struct GceFrameBuffer {
  typedef uint32_t Pixel;

  static const int kRedShift = 0;
  static const int kRedBits = 8;
  static const int kGreenShift = 8;
  static const int kGreenBits = 8;
  static const int kBlueShift = 16;
  static const int kBlueBits = 8;
  static const int kAlphaShift = 24;
  static const int kAlphaBits = 8;
};

// Sensors
struct gce_sensors_message {
  static constexpr const char* const kSensorsHALSocketName = "";
};
