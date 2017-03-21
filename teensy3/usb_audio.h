#ifndef USBaudio_h_
#define USBaudio_h_

#include "usb_desc.h"
#ifdef AUDIO_INTERFACE

#define FEATURE_MAX_VOLUME 0x100  // volume accepted from 0 to 0x100

#ifdef __cplusplus
extern "C" {
#endif
extern uint16_t *usb_audio_receive_buffer;
extern uint16_t usb_audio_receive_buffer1[];
extern uint16_t usb_audio_receive_buffer2[];
extern uint16_t usb_audio_transmit_buffer[];
extern void usb_audio_feedback_callback();
extern void usb_audio_receive_callback(unsigned int len);
extern unsigned int usb_audio_transmit_callback(void);
int usb_audio_set_feature(void *stp, uint8_t *buf);
int usb_audio_get_feature(void *stp, uint8_t *data, uint32_t *datalen);

extern uint32_t usb_audio_sync_feedback;
extern uint8_t usb_audio_receive_setting;
extern uint8_t usb_audio_transmit_setting;
#ifdef __cplusplus
}

// audio features supported
class usb_audio_features {
  public:
  int change;  // set to 1 when any value is changed
  int xtra;
  int mute;    // 1=mute, 0=unmute
  int volume[2];  // volume[0] is right, volume[1] is left; value from 0 to FEATURE_MAX_VOLUME
  usb_audio_features() : change(0), mute(0) { volume[0] = 0; volume[1] = 0; }
  float getVolume(int channel=0) { 
    if (mute) return 0.0;
    return (float)volume[channel] / (float)FEATURE_MAX_VOLUME; 
  }
};

#include "AudioStream.h"

class AudioInputUSB : public AudioStream
{
public:
	AudioInputUSB(void) : AudioStream(0, NULL) { begin(); }
	virtual void update(void);
	void begin(void);
	friend void usb_audio_receive_callback(unsigned int len);
	friend int usb_audio_set_feature(void *stp, uint8_t *buf);
	friend int usb_audio_get_feature(void *stp, uint8_t *data, uint32_t *datalen);

	static usb_audio_features features;

	float volume(int channel = 0) {
		if (features.mute) return 0.0;
		return (float)(features.volume[channel]) * (1.0 / (float)FEATURE_MAX_VOLUME);
	}
private:
	static bool update_responsibility;
	static audio_block_t *incoming_left;
	static audio_block_t *incoming_right;
	static audio_block_t *ready_left;
	static audio_block_t *ready_right;
	static uint16_t incoming_count;
	static uint8_t receive_flag;
};

class AudioOutputUSB : public AudioStream
{
public:
	AudioOutputUSB(void) : AudioStream(2, inputQueueArray) { begin(); }
	virtual void update(void);
	void begin(void);
	friend unsigned int usb_audio_transmit_callback(void);
private:
	static bool update_responsibility;
	static audio_block_t *left_1st;
	static audio_block_t *left_2nd;
	static audio_block_t *right_1st;
	static audio_block_t *right_2nd;
	static uint16_t offset_1st;
	audio_block_t *inputQueueArray[2];
};

#endif // __cplusplus
#endif // AUDIO_INTERFACE
#endif // USBaudio_h_
