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

int usb_aduio_sync_count_last;

usb_audio_features AudioInputUSB::features;

#define DMABUFATTR __attribute__ ((section(".dmabuffers"), aligned (4)))
uint16_t usb_audio_receive_buffer1[AUDIO_RX_SIZE/2] DMABUFATTR;
uint16_t usb_audio_receive_buffer2[AUDIO_RX_SIZE/2] DMABUFATTR;
uint32_t usb_audio_sync_feedback DMABUFATTR;
uint16_t *usb_audio_receive_buffer;

uint8_t usb_audio_receive_setting=0;

static uint32_t feedback_accumulator = 185042824;

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
	incoming_count = 0;
	incoming_left = NULL;
	incoming_right = NULL;
	ready_left = NULL;
	ready_right = NULL;
	receive_flag = 0;
	// update_responsibility = update_setup();
	// TODO: update responsibility is tough, partly because the USB
	// interrupts aren't sychronous to the audio library block size,
	// but also because the PC may stop transmitting data, which
	// means we no longer get receive callbacks from usb_dev.
	update_responsibility = false;
	usb_audio_sync_feedback = feedback_accumulator >> 8;
}

void usb_audio_feedback_callback() 
{
	int msize = read_buffer.size();
	if (msize < AUDIO_BLOCK_SAMPLES * 3) {
		usb_audio_sync_feedback++;
	}
	if (msize > AUDIO_BLOCK_SAMPLES * 5) {
		usb_audio_sync_feedback--;
	}

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

void AudioInputUSB::update(void)
{
	audio_block_t *left, *right;

	if (read_buffer.size() < AUDIO_BLOCK_SAMPLES * 2) {
		return;
	}

	left = allocate();
	if (left==NULL) {
		return;
	}
	right = allocate();
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


struct setup_struct {
  union {
    struct {
	uint8_t bmRequestType;
	uint8_t bRequest;
	union {
		struct {
			uint8_t bChannel;  // 0=main, 1=left, 2=right
			uint8_t bCS;       // Control Selector
		};
		uint16_t wValue;
	};
	union {
		struct {
			uint8_t bIfEp;     // type of entity
			uint8_t bEntityId; // UnitID, TerminalID, etc.
		};
		uint16_t wIndex;
	};
	uint16_t wLength;
    };
  };
};

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
