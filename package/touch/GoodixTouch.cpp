// SPDX-License-Identifier: BSD-2-Clause-Patent
// NX769J touch screen: Goodix GT9916R (Berlin-D) contacts to the iOS HID event system through sweet_hid_touch()
// (package/hid/SweetHID.cpp). Bus and power are in GoodixBus.cpp.
//
// Start (from bsd_init, unless nx769j-touch=0): vote L14B, power the chip in the vendor order, run the Berlin
// handshake (0xAA x8 to 0x10000, read back), read the version block (0x10014) and the IC info (0x10070) for the touch
// data address, then start a kernel thread that polls the interrupt latch and reads the event block:
//   header 8 bytes: status (bit 7 touch, bit 6 request), -, request_type (low nibble = contact count), -, checksum
//   contact 8 bytes: status (id 7:4, type 3:0), -, x, y, width (little endian), then a 16-bit byte-sum checksum
// and writes 0 to the status byte to acknowledge it (mainline goodix_berlin_core.c follows the same layout).
//
// Coordinates: the controller reports 16 units per panel pixel (goodix,panel-max-x/y 17856 x 39680 on the 1116 x 2480
// panel). package/display centres the 1125 x 2436 iOS canvas on the panel, so canvas = panel + (4, -22).
#include "GoodixBus.h"

#include <stdarg.h>
#include <string.h>
#include <IOKit/IOLib.h>
#include <pexpert/pexpert.h>

extern "C" {
kern_return_t kernel_thread_start(thread_continue_t continuation, void *parameter, thread_t *new_thread);
void thread_deallocate(thread_t thread);
void sweet_hid_touch(int finger, int down, int x, int y);
void nx769j_touch_start(void);
void nx769j_touch_report(void);
}

namespace nx769j_touch {
namespace {
constexpr unsigned kLines = 12, kWidth = 168;
constexpr uint32_t kBootOption = 0x10000, kVersion = 0x10014, kIcInfo = 0x10070;
constexpr unsigned kMaxContacts = 10, kHeader = 8, kContact = 8;
constexpr int kUnitsPerPixel = 16, kPanelW = 1116, kPanelH = 2480, kCanvasW = 1125, kCanvasH = 2436;
constexpr int kOffX = (kCanvasW - kPanelW) / 2, kOffY = (kCanvasH - kPanelH) / 2;
constexpr unsigned kIdlePollMs = 4, kActivePollMs = 1;

char gLines[kLines][kWidth];
unsigned gCount;
uint32_t gTouchAddr;
// Counters for the heartbeat line.
volatile uint32_t gPolls, gFrames, gContacts, gBadHead, gBadData, gResets, gDowns;
volatile int gLastX = -1, gLastY = -1;

size_t hex(char *out, size_t room, const uint8_t *buf, size_t len)
{
	size_t at = 0;
	for (size_t i = 0; i < len && at + 3 < room; i++) {
		at += (size_t)snprintf(out + at, room - at, "%02x", buf[i]);
	}
	return at;
}

bool sum_ok(const uint8_t *data, size_t size)
{
	if (size < 2) {
		return false;
	}
	uint32_t sum = 0;
	for (size_t i = 0; i < size - 2; i++) {
		sum += data[i];
	}
	return (uint16_t)sum == le16(data + size - 2);
}

bool handshake()
{
	uint8_t confirm[8], back[8] = {0};
	memset(confirm, 0xaa, sizeof(confirm));
	for (int tries = 0; tries < 5; tries++) {
		spi_write(kBootOption, confirm, sizeof(confirm));
		spi_read(kBootOption, back, sizeof(back));
		if (!memcmp(confirm, back, sizeof(back))) {
			return true;
		}
		IOSleep(5);
	}
	char text[32];
	hex(text, sizeof(text), back, sizeof(back));
	line("handshake FAIL (%s)", text);
	return false;
}

void read_version()
{
	uint8_t ver[28] = {0};                                         // rom_pid[6] rom_vid[3] . patch_pid[8] patch_vid[4] ...
	spi_read(kVersion, ver, sizeof(ver));
	char pid[9] = {0};
	for (unsigned i = 0; i < 8; i++) {
		pid[i] = (ver[10 + i] >= 0x20 && ver[10 + i] < 0x7f) ? (char)ver[10 + i] : '.';
	}
	line("chip %.6s patch %s vid %02x%02x%02x%02x sensor %u%s", reinterpret_cast<const char *>(ver), pid, ver[18],
	    ver[19], ver[20], ver[21], ver[23], sum_ok(ver, sizeof(ver)) ? "" : " (checksum bad)");
}

// IC info: le16 length, version block (16), feature block (10), drv/sen/button/force counts (4), then five
// count-prefixed le16 arrays, then the misc block whose touch_data_addr sits 44 bytes in.
uint32_t read_touch_addr()
{
	static uint8_t info[1024];
	spi_read(kIcInfo, info, 2);
	unsigned length = le16(info);
	if (length < 64 || length > sizeof(info)) {
		line("ic info length %u invalid", length);
		return 0;
	}
	spi_read(kIcInfo, info, length);
	if (!sum_ok(info, length)) {
		line("ic info checksum bad (length %u)", length);
		return 0;
	}
	unsigned offset = 2 + 16 + 10 + 4;
	for (int array = 0; array < 5; array++) {
		if (offset >= length) {
			break;
		}
		offset += 1 + 2u * info[offset];
	}
	if (offset + 48 > length) {
		line("ic info misc block out of range (offset %u length %u)", offset, length);
		return 0;
	}
	uint32_t addr = le32(info + offset + 44);
	line("ic info %u bytes: drv %u sen %u, touch data 0x%x", length, info[28], info[29], addr);
	return addr;
}

int canvas_x(int raw) { return raw / kUnitsPerPixel + kOffX; }
int canvas_y(int raw) { return raw / kUnitsPerPixel + kOffY; }

// One event block. Contacts not reported in a touch frame are lifted.
void handle_event(uint16_t &down, int (&lastX)[kMaxContacts], int (&lastY)[kMaxContacts])
{
	static uint8_t buf[kHeader + kMaxContacts * kContact + 2];
	spi_read(gTouchAddr, buf, kHeader + 2 * kContact + 2);
	if (buf[0] == 0) {
		return;
	}
	const uint8_t zero = 0;
	if (!sum_ok(buf, kHeader)) {
		gBadHead++;
		spi_write(gTouchAddr, &zero, 1);
		return;
	}
	gFrames++;
	if (buf[0] & 0x80) {
		unsigned n = buf[2] & 0x0f;
		if (n > kMaxContacts) {
			n = 0;
		}
		if (n > 2) {
			spi_read(gTouchAddr + kHeader + 2 * kContact + 2, buf + kHeader + 2 * kContact + 2, (n - 2) * kContact);
		}
		if (n && !sum_ok(buf + kHeader, n * kContact + 2)) {
			gBadData++;
		} else {
			uint16_t seen = 0;
			for (unsigned i = 0; i < n; i++) {
				const uint8_t *c = buf + kHeader + i * kContact;
				unsigned id = c[0] >> 4, type = c[0] & 0x0f;
				if (id >= kMaxContacts || type == 1 || type == 3) {    // stylus hover / stylus: not handled
					continue;
				}
				int x = canvas_x(le16(c + 2)), y = canvas_y(le16(c + 4));
				seen |= (uint16_t)(1u << id);
				lastX[id] = x;
				lastY[id] = y;
				gLastX = x;
				gLastY = y;
				gContacts++;
				if (!(down & (1u << id))) {
					gDowns++;
				}
				sweet_hid_touch((int)id, 1, x, y);
			}
			for (unsigned id = 0; id < kMaxContacts; id++) {
				if ((down & (1u << id)) && !(seen & (1u << id))) {
					sweet_hid_touch((int)id, 0, lastX[id], lastY[id]);
				}
			}
			down = seen;
		}
	}
	if ((buf[0] & 0x40) && buf[2] == 3) {                          // request: reset
		gResets++;
		chip_reset_pulse();
	}
	spi_write(gTouchAddr, &zero, 1);
}

void worker(void *, wait_result_t)
{
	uint16_t down = 0;
	int lastX[kMaxContacts] = {0}, lastY[kMaxContacts] = {0};
	for (;;) {
		gPolls++;
		if (irq_pending()) {
			handle_event(down, lastX, lastY);
			IOSleep(kActivePollMs);
		} else {
			IOSleep(down ? kActivePollMs : kIdlePollMs);
		}
	}
}

void start()
{
	bus_init();
	if (!avdd_on()) {
		return;
	}
	chip_power(true);
	if (!handshake()) {
		chip_power(false);
		return;
	}
	IOSleep(100);                                                   // the firmware finishes booting
	read_version();
	gTouchAddr = read_touch_addr();
	if (!gTouchAddr) {
		return;
	}
	const uint8_t zero = 0;
	spi_write(gTouchAddr, &zero, 1);
	thread_t thread;
	if (kernel_thread_start(worker, nullptr, &thread) != KERN_SUCCESS) {
		line("worker thread not started");
		return;
	}
	thread_deallocate(thread);
	line("running: irq level %d", irq_level());
}
} // namespace

void line(const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	char *out = gLines[gCount < kLines ? gCount : kLines - 1];
	vsnprintf(out, kWidth, fmt, ap);
	va_end(ap);
	if (gCount < kLines) {
		gCount++;
	}
	printf("NX769J TOUCH %s\n", out);
}

} // namespace nx769j_touch

void
nx769j_touch_start(void)
{
	int enabled = 1;
	PE_parse_boot_argn("nx769j-touch", &enabled, sizeof(enabled));
	if (enabled) {
		nx769j_touch::start();
	}
}

void
nx769j_touch_report(void)
{
	using namespace nx769j_touch;
	for (unsigned i = 0; i < gCount; i++) {
		printf("NX769J TOUCH %s\n", gLines[i]);
	}
	if (gTouchAddr) {
		printf("NX769J TOUCH polls %u frames %u contacts %u downs %u bad %u/%u resets %u last %d,%d irq %d\n", gPolls,
		    gFrames, gContacts, gDowns, gBadHead, gBadData, gResets, gLastX, gLastY, irq_level());
	}
}
