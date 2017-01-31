#ifdef USB_AUDIO_MIDI_KEYBOARD
  #define VENDOR_ID             0x16C0
  #define PRODUCT_ID            0x0485
  #define MANUFACTURER_NAME     {'V','i','n','d','o','r',' ','M','u','s','i','c',',',' ','I','n','c','.'}
  #define MANUFACTURER_NAME_LEN 18
  #define PRODUCT_NAME          {'V','i','n','d','o','r'}
  #define PRODUCT_NAME_LEN      6
  #define EP0_SIZE              64
  #define NUM_ENDPOINTS         9
  #define NUM_USB_BUFFERS       30
  #define NUM_INTERFACE         7

  #define MIDI_INTERFACE        0       // MIDI
  #define MIDI_TX_ENDPOINT      5
  #define MIDI_TX_SIZE          64
  #define MIDI_RX_ENDPOINT      6
  #define MIDI_RX_SIZE          64

  #define KEYBOARD_INTERFACE    1       // Keyboard
  #define KEYBOARD_ENDPOINT     3
  #define KEYBOARD_SIZE         8
  #define KEYBOARD_INTERVAL     1

  #define SEREMU_INTERFACE      2       // Serial emulation
  #define SEREMU_TX_ENDPOINT    1
  #define SEREMU_TX_SIZE        64
  #define SEREMU_TX_INTERVAL    1
  #define SEREMU_RX_ENDPOINT    2
  #define SEREMU_RX_SIZE        32
  #define SEREMU_RX_INTERVAL    2

  //#define KEYMEDIA_INTERFACE    2       // Keyboard Media Keys
  #define KEYMEDIA_INTERFACE    3       // Keyboard Media Keys
  #define KEYMEDIA_ENDPOINT     4
  #define KEYMEDIA_SIZE         8
  #define KEYMEDIA_INTERVAL     4

  //#define AUDIO_INTERFACE       3       // Audio (uses 3 consecutive interfaces)
  #define AUDIO_INTERFACE       4       // Audio (uses 3 consecutive interfaces)
  #define AUDIO_TX_ENDPOINT     7
  #define AUDIO_TX_SIZE         180
  #define AUDIO_RX_ENDPOINT     8
  #define AUDIO_RX_SIZE         180
  #define AUDIO_SYNC_ENDPOINT   9

  #define ENDPOINT1_CONFIG      ENDPOINT_TRANSIMIT_ONLY
  #define ENDPOINT2_CONFIG      ENDPOINT_RECEIVE_ONLY
  #define ENDPOINT3_CONFIG      ENDPOINT_TRANSIMIT_ONLY
  #define ENDPOINT4_CONFIG      ENDPOINT_TRANSIMIT_ONLY
  #define ENDPOINT5_CONFIG      ENDPOINT_TRANSIMIT_ONLY
  #define ENDPOINT6_CONFIG      ENDPOINT_RECEIVE_ONLY
  #define ENDPOINT7_CONFIG      ENDPOINT_TRANSMIT_ISOCHRONOUS
  #define ENDPOINT8_CONFIG      ENDPOINT_RECEIVE_ISOCHRONOUS
  #define ENDPOINT9_CONFIG      ENDPOINT_TRANSMIT_ISOCHRONOUS
#endif
