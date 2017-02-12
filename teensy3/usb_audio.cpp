/* Teensyduino Core Library
 * http://www.pjrc.com/teensy/
 * Copyright (c) 2016 PJRC.COM, LLC.
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * 1. The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * 2. If the Software is incorporated into a build system that allows
 * selection among a list of target devices, then similar target
 * devices manufactured by PJRC.COM must be included in the list of
 * target devices and selectable in the same manner.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include "usb_dev.h"
#include "usb_audio.h"
#include "HardwareSerial.h"
#include <string.h> // for memcpy()

#ifdef AUDIO_INTERFACE // defined by usb_dev.h -> usb_desc.h
#if F_CPU >= 20000000

// Uncomment this to work around a limitation in Macintosh adaptive rates
// This is not a perfect solution.  Details here:
// https://forum.pjrc.com/threads/34855-Distorted-audio-when-using-USB-input-on-Teensy-3-1
//#define MACOSX_ADAPTIVE_LIMIT

bool AudioInputUSB::update_responsibility;
audio_block_t * AudioInputUSB::incoming_left;
audio_block_t * AudioInputUSB::incoming_right;
audio_block_t * AudioInputUSB::ready_left;
audio_block_t * AudioInputUSB::ready_right;
uint16_t AudioInputUSB::incoming_count;
uint8_t AudioInputUSB::receive_flag;

usb_audio_features AudioInputUSB::features;

#define DMABUFATTR __attribute__ ((section(".dmabuffers"), aligned (4)))
uint16_t usb_audio_receive_buffer1[AUDIO_RX_SIZE/2] DMABUFATTR;
uint16_t usb_audio_receive_buffer2[AUDIO_RX_SIZE/2] DMABUFATTR;
uint32_t usb_audio_sync_feedback DMABUFATTR;
uint16_t *usb_audio_receive_buffer;

int usb_aduio_sync_count_lt;
int usb_aduio_sync_count_44;
int usb_aduio_sync_count_45;
int usb_aduio_sync_count_gt;
int usb_aduio_sync_count_last;
int usb_aduio_sync_count_samples;
int usb_aduio_sync_count_packets;

int usb_aduio_sync_count_slow;
int usb_aduio_sync_count_fast;
int usb_audio_count_zero;
int usb_aduio_sync_rate;
int usb_audio_sync_mem;
int usb_audio_sync_updates;
int usb_audio_sync_updates_start;
int usb_audio_sync_updates_last;

uint8_t usb_audio_receive_setting=0;

class AudioBuffer {
private:
	const static int buffer_len = AUDIO_BLOCK_SAMPLES * 10;
	int16_t buffer[buffer_len];
	int writeidx;
	int readidx;
public:
	void add(uint16_t *data, int len) {
		for (int i = 0; i < len * 2; i++) {
			buffer[writeidx++] = data[i];
			if (writeidx >= buffer_len) writeidx = 0;
		}
	}
	void copy(int16_t *left, int16_t *right, int len) {
		for (int i = 0; i < len; i++) {
			left[i] = buffer[readidx++];
			if (readidx >= buffer_len) readidx = 0;
			right[i] = buffer[readidx++];
			if (readidx >= buffer_len) readidx = 0;
		}
	}
	int size() {
		if (readidx <= writeidx) return writeidx - readidx;
		return buffer_len - readidx + writeidx;
	}
};

AudioBuffer read_buffer;

void AudioInputUSB::begin(void)
{
#if 0
	incoming_count = 0;
	incoming_left = NULL;
	incoming_right = NULL;
	ready_left = NULL;
	ready_right = NULL;
	receive_flag = 0;
#endif
	// update_responsibility = update_setup();
	// TODO: update responsibility is tough, partly because the USB
	// interrupts aren't sychronous to the audio library block size,
	// but also because the PC may stop transmitting data, which
	// means we no longer get receive callbacks from usb_dev.
	update_responsibility = false;
	usb_audio_sync_feedback = 722602; //722534; // feedback_accumulator >> 8;
}

#if 0
static void copy_to_buffers(const uint32_t *src, int16_t *left, int16_t *right, unsigned int len)
{
	uint32_t *target = (uint32_t*) src + len; 
	while ((src < target) && (((uintptr_t) left & 0x02) != 0)) {
		uint32_t n = *src++;
		*left++ = n & 0xFFFF;
		*right++ = n >> 16;
	}

	while ((src < target - 2)) {
		uint32_t n1 = *src++;
		uint32_t n = *src++;
		*(uint32_t *)left = (n1 & 0xFFFF) | ((n & 0xFFFF) << 16);
		left+=2;
		*(uint32_t *)right = (n1 >> 16) | ((n & 0xFFFF0000)) ;
		right+=2;
	}

	while ((src < target)) {
		uint32_t n = *src++;
		*left++ = n & 0xFFFF;
		*right++ = n >> 16;
	}
}
#endif

void usb_audio_feedback_callback() 
{
#if 0
	if (usb_aduio_sync_count_slow) {
		usb_audio_sync_feedback += 3;
		usb_aduio_sync_count_slow--;
		usb_aduio_sync_count_fast = 0;
	}
	else if (usb_aduio_sync_count_fast) {
		usb_audio_sync_feedback -= 3;
		usb_aduio_sync_count_fast--;
	}
	else {
		if (usb_aduio_sync_rate > 0) {
			// if (usb_aduio_sync_rate > 45 || usb_aduio_sync_rate - prate > 2)
				usb_audio_sync_feedback--;
		}
		else if (usb_aduio_sync_rate < 0) {
			// if (usb_aduio_sync_rate < -45 || usb_aduio_sync_rate - prate < 0)
				usb_audio_sync_feedback++;
		}
	}
	usb_aduio_sync_rate = 0;
#endif

#if 1
	int msize = read_buffer.size();
	if (msize < AUDIO_BLOCK_SAMPLES * 3) {
		usb_audio_sync_feedback++;
	}
	if (msize > AUDIO_BLOCK_SAMPLES * 5) {
		usb_audio_sync_feedback--;
	}
#endif


#if 0	
	static int usb_audio_sync_feedback_adjust;
	static uint32_t prate;
	if (usb_aduio_sync_count_slow) {
		usb_aduio_sync_count_slow--;
		usb_audio_sync_feedback_adjust+=3;
		usb_aduio_sync_count_fast = 0;
		serial_print("slow\n");
	}
	else if (usb_aduio_sync_count_fast) {
		usb_aduio_sync_count_fast-=3;
		usb_audio_sync_feedback_adjust--;
		serial_print("fast\n");
	}
	else {
		if (usb_aduio_sync_rate > 0) {
			if (usb_aduio_sync_rate > 45 || usb_aduio_sync_rate - prate > 2) {
				usb_audio_sync_feedback_adjust--;
				serial_print("+\n");
			}
		}
		else if (usb_aduio_sync_rate < 0) {
			if (usb_aduio_sync_rate < -45 || usb_aduio_sync_rate - prate < 0) {
				usb_audio_sync_feedback_adjust++;
				serial_print("-\n");
			}
		}
	}
	prate = usb_aduio_sync_rate;
#endif

#if 0
	// Send back how many samples the computer should send between every SOF packet (which is approx 1ms).
	// Returned in 10.14 format (hence all the ">> 14" below).
	// Formula is: cpu_ticks_per_sof * usb_samples_per_sec / cpu_ticks_per_sec
	// In a perfect world, the answer is 44.1 packets per SOF cycle (or ~722534 in 10.14 format).
	// But this is not the case because clocks never run precisely in sync.
	extern long long usb_audio_sync_sof;
	extern long long usb_audio_sync_sof_last;
	uint32_t cpu_ticks_per_sof = usb_audio_sync_sof - usb_audio_sync_sof_last;

	if (cpu_ticks_per_sof < 96500 && cpu_ticks_per_sof > 95500) {
		const uint32_t usb_audio_sync_clock = 44100; // Actually: F_CPU * MCLK_MULT / MCLK_DIV / 256;
		// uint32_t usb_audio_sync_clock = F_CPU * 2 / 7; // using original divisors

		const uint32_t cpu_ticks_per_sec = F_CPU;
		long long x = cpu_ticks_per_sof;
		x <<= 14;
		x *= usb_audio_sync_clock;
		x /= cpu_ticks_per_sec;
		usb_audio_sync_feedback = x + usb_audio_sync_feedback_adjust;
	}

	usb_aduio_sync_count_lt = usb_aduio_sync_count_44 = usb_aduio_sync_count_45 = usb_aduio_sync_count_gt = 0;
	usb_aduio_sync_count_samples = usb_aduio_sync_count_packets = 0;
	usb_audio_count_zero = 0;
#endif
#if 0
		const uint32_t usb_audio_sync_clock_min = (usb_audio_sync_clock << 14) / 1000; // min samples per packet
		const uint32_t usb_audio_sync_clock_max = ((usb_audio_sync_clock) << 14) / 1000; // max samples per packet
		// Get the average number of samples per packet, should be approx 44.1 (in 10.14 format)
		uint32_t avg_samples_per_packet = (usb_aduio_sync_count_samples << 14) / usb_aduio_sync_count_packets;
		// Is it in the range of what we want?
		if (avg_samples_per_packet < usb_audio_sync_clock_min) { // too low, adjust up
			usb_audio_sync_feedback += 1;
		}
		else if (avg_samples_per_packet > usb_audio_sync_clock_max) { // too high, adjust down
			usb_audio_sync_feedback -= 1;
		}
#endif

	if (usb_audio_sync_feedback > 729759) usb_audio_sync_feedback = 729759;
	else if (usb_audio_sync_feedback < 715306) usb_audio_sync_feedback = 715306;
}

// Called from the USB interrupt when an isochronous packet arrives
// we must completely remove it from the receive buffer before returning
//
void usb_audio_receive_callback(unsigned int len) {
	len >>= 2; // 1 sample = 4 bytes: 2 left, 2 right
	usb_aduio_sync_count_last = len;
	read_buffer.add(usb_audio_receive_buffer, len);
}

#if 0
void usb_audio_receive_callback(unsigned int len)
{
	unsigned int count, avail;
	audio_block_t *left, *right;
	const uint32_t *data;

	ONCE

	AudioInputUSB::receive_flag = 1;

	len >>= 2; // 1 sample = 4 bytes: 2 left, 2 right
	data = (const uint32_t *)usb_audio_receive_buffer;

	usb_aduio_sync_count_last = len;
	
	if (len < 44) usb_aduio_sync_count_lt++;
	else if (len == 44) usb_aduio_sync_count_44++;
	else if (len == 45) usb_aduio_sync_count_45++;
	else usb_aduio_sync_count_gt++;

	usb_aduio_sync_count_samples += len;
	usb_aduio_sync_count_packets++;;

	if (data[0]==0 && data[1]==0 && data[3]==0) {
		usb_audio_count_zero++;
	}

	count = AudioInputUSB::incoming_count;
	left = AudioInputUSB::incoming_left;
	right = AudioInputUSB::incoming_right;
	if (left == NULL) {
		left = AudioStream::allocate(__LINE__);
		if (left == NULL) return;
		AudioInputUSB::incoming_left = left;
	}
	if (right == NULL) {
		right = AudioStream::allocate(__LINE__);
		if (right == NULL) return;
		AudioInputUSB::incoming_right = right;
	}

	while (len > 0) {
		avail = AUDIO_BLOCK_SAMPLES - count;
		if (len < avail) {
			copy_to_buffers(data, left->data + count, right->data + count, len);
			AudioInputUSB::incoming_count = count + len;
			return;
		} else if (avail > 0) {
			copy_to_buffers(data, left->data + count, right->data + count, avail);
			data += avail;
			len -= avail;
			if (AudioInputUSB::ready_left || AudioInputUSB::ready_right) {
				if (len > 0) {
				// buffer overrun, PC sending too fast
					usb_aduio_sync_count_fast = 3;
					AudioInputUSB::incoming_count = count + avail;
				}
				return;
			}
			send:
			__disable_irq();
			// if (AudioInputUSB::ready_left != NULL) __asm volatile("bkpt");
			// if (AudioInputUSB::ready_right != NULL) __asm volatile("bkpt");
			AudioInputUSB::ready_left = left;
			AudioInputUSB::ready_right = right;
			__enable_irq();
			//if (AudioInputUSB::update_responsibility) AudioStream::update_all();
			left = AudioStream::allocate(__LINE__);
			if (left == NULL) {
				AudioInputUSB::incoming_left = NULL;
				AudioInputUSB::incoming_right = NULL;
				AudioInputUSB::incoming_count = 0;
				return;
			}
			right = AudioStream::allocate(__LINE__);
			if (right == NULL) {
				AudioStream::release(left);
				AudioInputUSB::incoming_left = NULL;
				AudioInputUSB::incoming_right = NULL;
				AudioInputUSB::incoming_count = 0;
				return;
			}
			AudioInputUSB::incoming_left = left;
			AudioInputUSB::incoming_right = right;
			count = 0;
		} else {
			if (AudioInputUSB::ready_left || AudioInputUSB::ready_right) return;
			goto send; // recover from buffer overrun
		}
	}
	AudioInputUSB::incoming_count = count;
}
#endif

void AudioInputUSB::update(void)
{
	audio_block_t *left, *right;

	if (read_buffer.size() < AUDIO_BLOCK_SAMPLES * 2) {
		usb_aduio_sync_count_slow++;
		return;
	}

	left = allocate(__LINE__);
	if (left==NULL) {
		return;
	}
	right = allocate(__LINE__);
	if (right == NULL) {
		release(left);
		return;
	}

	read_buffer.copy(left->data, right->data, AUDIO_BLOCK_SAMPLES);

	transmit(left, 0);
	release(left);
	transmit(right, 1);
	release(right);
}

#if 0
void AudioInputUSB::update(void)
{
	audio_block_t *left, *right;

	ONCE

	__disable_irq();
	left = ready_left;
	ready_left = NULL;
	right = ready_right;
	ready_right = NULL;
	uint16_t c = incoming_count;
	uint8_t f = receive_flag;
	receive_flag = 0;
	__enable_irq();

	if (f) {
		// target pending buffer half full
		usb_aduio_sync_rate = c - AUDIO_BLOCK_SAMPLES/2;
		if (!left || !right) {
			usb_aduio_sync_count_slow=5;
		}
	}

	extern uint32_t systick_millis_count;
	if (usb_audio_sync_updates == 0) usb_audio_sync_updates_start = systick_millis_count;
	usb_audio_sync_updates++;
	usb_audio_sync_updates_last = systick_millis_count;
	usb_audio_sync_mem = AudioMemoryUsage();

	if (left) {
		transmit(left, 0);
		release(left);
	}
	if (right) {
		transmit(right, 1);
		release(right);
	}
}

#endif





bool AudioOutputUSB::update_responsibility;
audio_block_t * AudioOutputUSB::left_1st;
audio_block_t * AudioOutputUSB::left_2nd;
audio_block_t * AudioOutputUSB::right_1st;
audio_block_t * AudioOutputUSB::right_2nd;
uint16_t AudioOutputUSB::offset_1st;


uint16_t usb_audio_transmit_buffer[AUDIO_TX_SIZE/2] DMABUFATTR;
uint8_t usb_audio_transmit_setting=0;

void AudioOutputUSB::begin(void)
{
	update_responsibility = false;
	left_1st = NULL;
	right_1st = NULL;
}

static void copy_from_buffers(uint32_t *dst, int16_t *left, int16_t *right, unsigned int len)
{
	// TODO: optimize...
	while (len > 0) {
		*dst++ = (*right++ << 16) | (*left++ & 0xFFFF);
		len--;
	}
}

void AudioOutputUSB::update(void)
{
	audio_block_t *left, *right;

	left = receiveReadOnly(0); // input 0 = left channel
	right = receiveReadOnly(1); // input 1 = right channel
	if (usb_audio_transmit_setting == 0) {
		if (left) release(left);
		if (right) release(right);
		if (left_1st) { release(left_1st); left_1st = NULL; }
		if (left_2nd) { release(left_2nd); left_2nd = NULL; }
		if (right_1st) { release(right_1st); right_1st = NULL; }
		if (right_2nd) { release(right_2nd); right_2nd = NULL; }
		offset_1st = 0;
		return;
	}
	if (left == NULL) {
		if (right == NULL) return;
		right->ref_count++;
		left = right;
	} else if (right == NULL) {
		left->ref_count++;
		right = left;
	}
	__disable_irq();
	if (left_1st == NULL) {
		left_1st = left;
		right_1st = right;
		offset_1st = 0;
	} else if (left_2nd == NULL) {
		left_2nd = left;
		right_2nd = right;
	} else {
		// buffer overrun - PC is consuming too slowly
		audio_block_t *discard1 = left_1st;
		left_1st = left_2nd;
		left_2nd = left;
		audio_block_t *discard2 = right_1st;
		right_1st = right_2nd;
		right_2nd = right;
		offset_1st = 0; // TODO: discard part of this data?
		//serial_print("*");
		release(discard1);
		release(discard2);
	}
	__enable_irq();
}


// Called from the USB interrupt when ready to transmit another
// isochronous packet.  If we place data into the transmit buffer,
// the return is the number of bytes.  Otherwise, return 0 means
// no data to transmit
unsigned int usb_audio_transmit_callback(void)
{
	static uint32_t count=5;
	uint32_t avail, num, target, offset, len=0;
	audio_block_t *left, *right;

	if (usb_aduio_sync_count_last) {
		target = usb_aduio_sync_count_last;
		usb_aduio_sync_count_last = 0;
	}
	else {
		if (++count < 9) {   // TODO: dynamic adjust to match USB rate
			target = 44;
		} else {
			count = 0;
			target = 45;
		}		
	}

	while (len < target) {
		num = target - len;
		left = AudioOutputUSB::left_1st;
		if (left == NULL) {
			// buffer underrun - PC is consuming too quickly
			memset(usb_audio_transmit_buffer + len, 0, num * 4);
			//serial_print("%");
			break;
		}
		right = AudioOutputUSB::right_1st;
		offset = AudioOutputUSB::offset_1st;

		avail = AUDIO_BLOCK_SAMPLES - offset;
		if (num > avail) num = avail;

		copy_from_buffers((uint32_t *)usb_audio_transmit_buffer + len,
			left->data + offset, right->data + offset, num);
		len += num;
		offset += num;
		if (offset >= AUDIO_BLOCK_SAMPLES) {
			AudioStream::release(left);
			AudioStream::release(right);
			AudioOutputUSB::left_1st = AudioOutputUSB::left_2nd;
			AudioOutputUSB::left_2nd = NULL;
			AudioOutputUSB::right_1st = AudioOutputUSB::right_2nd;
			AudioOutputUSB::right_2nd = NULL;
			AudioOutputUSB::offset_1st = 0;
		} else {
			AudioOutputUSB::offset_1st = offset;
		}
	}
	return target * 4;
}

int usb_audio_get_feature(void *stp, uint8_t *data, uint32_t *datalen)
{
	struct setup_struct setup = *((struct setup_struct *)stp);
	if (setup.bmRequestType==0xA1) { // should check bRequest, bChannel, and UnitID
			if (setup.bCS==0x01) { // mute
				data[0] = AudioInputUSB::features.mute;  // 1=mute, 0=unmute
				*datalen = 1;
				return 1;
			}
			else if (setup.bCS==0x02) { // volume
				if (setup.bRequest==0x81) { // GET_CURR
					data[0] = AudioInputUSB::features.volume[setup.bChannel-1] & 0xFF;
					data[1] = (AudioInputUSB::features.volume[setup.bChannel-1] >> 8) & 0xFF;
				}
				else if (setup.bRequest==0x82) { // GET_MIN
					//serial_print("vol get_min\n");
					data[0] = 0;     // min level is 0
					data[1] = 0;
				}
				else if (setup.bRequest==0x83) { // GET_MAX
					data[0] = FEATURE_MAX_VOLUME & 0xFF;  // max level, for range of 0 to MAX
					data[1] = (FEATURE_MAX_VOLUME>>8) & 0x0F;
				}
				else if (setup.bRequest==0x84) { // GET_RES
					data[0] = 1; // increment vol by by 1
					data[1] = 0;
				}
				else { // pass over SET_MEM, etc.
					return 0;
				}
				*datalen = 2;
				return 1;
			}
	}
	return 0;
}

int usb_audio_set_feature(void *stp, uint8_t *buf) 
{
	struct setup_struct setup = *((struct setup_struct *)stp);
	if (setup.bmRequestType==0x21) { // should check bRequest, bChannel and UnitID
			if (setup.bCS==0x01) { // mute
				if (setup.bRequest==0x01) { // SET_CUR
					AudioInputUSB::features.mute = buf[0]; // 1=mute,0=unmute
					AudioInputUSB::features.change = 1;
					return 1;
				}
			}
			else if (setup.bCS==0x02) { // volume
				if (setup.bRequest==0x01) { // SET_CUR
					AudioInputUSB::features.xtra = setup.bChannel;
					AudioInputUSB::features.volume[setup.bChannel-1] = buf[0] + (buf[1]<<8);
					AudioInputUSB::features.change = 1;
					return 1;
				}
			}
	}
	return 0;
}


#endif // F_CPU
#endif // AUDIO_INTERFACE
