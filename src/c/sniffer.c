#include "sniffer.h"

// https://mimesniff.spec.whatwg.org/
// Copyright © WHATWG (Apple, Google, Mozilla, Microsoft).

// WHATWG MIME Sniffing Standard - MP4 signature algorithm
static bool match_mp4_signature(uint8_t const *const data, size_t const len) {
  if (len < 12) {
    return false;
  }

  // Get box size from first 4 bytes (big-endian)
  uint32_t const box_size =
      ((uint32_t)data[0] << 24) | ((uint32_t)data[1] << 16) | ((uint32_t)data[2] << 8) | (uint32_t)data[3];
  if (len < box_size || box_size % 4 != 0) {
    return false;
  }

  // Check for "ftyp" at bytes 4-7 (0x66 0x74 0x79 0x70)
  if (data[4] != 0x66 || data[5] != 0x74 || data[6] != 0x79 || data[7] != 0x70) {
    return false;
  }

  // Check if bytes 8-10 are "mp4" (0x6D 0x70 0x34)
  if (len > 10 && data[8] == 0x6D && data[9] == 0x70 && data[10] == 0x34) {
    return true;
  }

  // Check extended brands starting at byte 16
  for (size_t bytes_read = 16; bytes_read < box_size && bytes_read + 2 < len; bytes_read += 4) {
    if (data[bytes_read] == 0x6D && data[bytes_read + 1] == 0x70 && data[bytes_read + 2] == 0x34) {
      return true;
    }
  }
  return false;
}

// Parse vint according to WHATWG spec
static size_t
parse_vint(uint8_t const *const data, size_t const len, size_t const index, uint64_t *const parsed_number) {
  if (index >= len) {
    return 0;
  }

  enum {
    max_vint_length = 8,
  };
  uint8_t mask = 128; // 0x80
  size_t number_size = 1;

  // Determine number size
  for (size_t i = 0; i < max_vint_length; i++) {
    if ((data[index] & mask) != 0) {
      break;
    }
    mask >>= 1;
    number_size++;
  }

  if (number_size > max_vint_length || index + number_size > len) {
    return 0;
  }

  // Parse number
  *parsed_number = data[index] & (mask - 1);
  for (size_t i = 1; i < number_size; i++) {
    *parsed_number = (*parsed_number << 8) | data[index + i];
  }

  return number_size;
}

// Match padded sequence according to WHATWG spec
static bool match_padded_sequence_webm(uint8_t const *const sequence,
                                       size_t const offset,
                                       size_t const end,
                                       size_t const length,
                                       uint8_t const *const pattern,
                                       size_t const pattern_len) {
  if (length <= end) {
    return false;
  }
  if (end < offset || end - offset < pattern_len) {
    return false;
  }

  // Look for pattern in range [offset, end]
  for (size_t i = offset; i <= end - pattern_len; i++) {
    bool match = true;
    for (size_t j = 0; j < pattern_len; j++) {
      if (sequence[i + j] != pattern[j]) {
        match = false;
        break;
      }
    }
    if (match) {
      // Check all preceding bytes in range are 0x00
      for (size_t k = offset; k < i; k++) {
        if (sequence[k] != 0x00) {
          return false;
        }
      }
      return true;
    }
  }
  return false;
}

// WHATWG MIME Sniffing Standard - WebM signature algorithm
static bool match_webm_signature(uint8_t const *const data, size_t const len) {
  if (len < 4) {
    return false;
  }

  // Check EBML header (0x1A 0x45 0xDF 0xA3)
  if (data[0] != 0x1a || data[1] != 0x45 || data[2] != 0xdf || data[3] != 0xa3) {
    return false;
  }

  // Let iter be 4
  for (size_t iter = 4; iter < len && iter < 38; iter++) {
    // If the two bytes are equal to 0x42 0x82
    if (iter + 1 < len && data[iter] == 0x42 && data[iter + 1] == 0x82) {
      // Increment iter by 2
      iter += 2;
      if (iter >= len) {
        break;
      }

      // Parse vint starting at sequence[iter]
      uint64_t parsed_number;
      size_t number_size = parse_vint(data, len, iter, &parsed_number);
      if (number_size == 0) {
        break;
      }

      // Increment iter by number_size
      iter += number_size;
      if (iter >= len - 4) {
        break;
      }

      // Match padded sequence "webm" (0x77 0x65 0x62 0x6D)
      static uint8_t const webm_pattern[] = {0x77, 0x65, 0x62, 0x6D};
      size_t end = iter + parsed_number;
      if (match_padded_sequence_webm(data, iter, end, len, webm_pattern, 4)) {
        return true;
      }
      break;
    }
    // iter is incremented by the loop
  }
  return false;
}

// Parse MP3 frame according to WHATWG spec
static void parse_mp3_frame(uint8_t const *const sequence,
                            size_t const s,
                            uint8_t *const version,
                            uint32_t *const bitrate,
                            uint32_t *const freq,
                            uint8_t *const pad) {
  // MPEG bitrate tables (kbps) - WHATWG MIME Sniffing Standard
  static uint32_t const mpeg1_layer1_rates[] = {0,
                                                32000,
                                                64000,
                                                96000,
                                                128000,
                                                160000,
                                                192000,
                                                224000,
                                                256000,
                                                288000,
                                                320000,
                                                352000,
                                                384000,
                                                416000,
                                                448000,
                                                0};
  static uint32_t const mpeg1_layer2_rates[] = {
      0, 32000, 48000, 56000, 64000, 80000, 96000, 112000, 128000, 160000, 192000, 224000, 256000, 320000, 384000, 0};
  static uint32_t const mpeg1_layer3_rates[] = {
      0, 32000, 40000, 48000, 56000, 64000, 80000, 96000, 112000, 128000, 160000, 192000, 224000, 256000, 320000, 0};
  static uint32_t const mpeg2_layer1_rates[] = {
      0, 32000, 48000, 56000, 64000, 80000, 96000, 112000, 128000, 144000, 160000, 176000, 192000, 224000, 256000, 0};
  static uint32_t const mpeg2_layer23_rates[] = {
      0, 8000, 16000, 24000, 32000, 40000, 48000, 56000, 64000, 80000, 96000, 112000, 128000, 144000, 160000, 0};

  // Select appropriate bitrate table based on MPEG version and layer
  // Extract layer field: (byte[s+1] & 0x06) >> 1
  *version = (sequence[s + 1] & 0x18) >> 3;
  uint8_t const layer = (sequence[s + 1] & 0x06) >> 1;
  uint8_t const bitrate_index = (sequence[s + 2] & 0xf0) >> 4;
  if (*version == 3) { // MPEG1
    if (layer == 3) {  // Layer 1
      *bitrate = mpeg1_layer1_rates[bitrate_index];
    } else if (layer == 2) { // Layer 2
      *bitrate = mpeg1_layer2_rates[bitrate_index];
    } else if (layer == 1) { // Layer 3
      *bitrate = mpeg1_layer3_rates[bitrate_index];
    } else {
      *bitrate = 0; // Reserved layer
    }
  } else if (*version == 2 || *version == 0) { // MPEG2 or MPEG2.5
    if (layer == 3) {                          // Layer 1
      *bitrate = mpeg2_layer1_rates[bitrate_index];
    } else if (layer == 2 || layer == 1) { // Layer 2 or 3
      *bitrate = mpeg2_layer23_rates[bitrate_index];
    } else {
      *bitrate = 0; // Reserved layer
    }
  } else { // Reserved version
    *bitrate = 0;
  }

  // MPEG sample rate tables (Hz) - WHATWG MIME Sniffing Standard
  static uint32_t const mpeg1_sample_rates[] = {44100, 48000, 32000, 0};
  static uint32_t const mpeg2_sample_rates[] = {22050, 24000, 16000, 0};
  static uint32_t const mpeg25_sample_rates[] = {11025, 12000, 8000, 0};
  // Select appropriate sample rate table based on MPEG version
  uint8_t const samplerate_index = (sequence[s + 2] & 0x0c) >> 2;
  if (*version == 3) { // MPEG1
    *freq = mpeg1_sample_rates[samplerate_index];
  } else if (*version == 2) { // MPEG2
    *freq = mpeg2_sample_rates[samplerate_index];
  } else if (*version == 0) { // MPEG2.5
    *freq = mpeg25_sample_rates[samplerate_index];
  } else { // Reserved
    *freq = 0;
  }

  *pad = (sequence[s + 2] & 0x02) >> 1;
}

// Compute MP3 frame size according to WHATWG spec
static uint32_t compute_mp3_frame_size(
    uint8_t const version, uint8_t const layer, uint32_t const bitrate, uint32_t const freq, uint8_t const pad) {
  // MPEG version and layer specific scale factors
  uint32_t scale;
  if (version == 3) { // MPEG1
    if (layer == 3) { // Layer 1
      scale = 48;     // MPEG1 Layer 1: 48 samples per frame
    } else {          // Layer 2/3
      scale = 144;    // MPEG1 Layer 2/3: 1152 samples per frame / 8
    }
  } else {            // MPEG2/MPEG2.5
    if (layer == 3) { // Layer 1
      scale = 24;     // MPEG2 Layer 1: 24 samples per frame
    } else {          // Layer 2/3
      scale = 72;     // MPEG2 Layer 2/3: 576 samples per frame / 8
    }
  }
  uint32_t size = bitrate * scale / freq;
  if (pad != 0) {
    size++;
  }
  return size;
}

// Match MP3 header according to WHATWG spec
static bool match_mp3_header(uint8_t const *const sequence, size_t const length, size_t const s) {
  if (s + 3 >= length) {
    return false;
  }

  // Check sync pattern
  if (sequence[s] != 0xff || (sequence[s + 1] & 0xe0) != 0xe0) {
    return false;
  }

  // Let layer be the result of sequence[s + 1] & 0x06 >> 1
  uint8_t const layer = (sequence[s + 1] & 0x06) >> 1;
  if (layer == 0) {
    return false;
  }

  // Let bit-rate be sequence[s + 2] & 0xf0 >> 4
  uint8_t const bit_rate = (sequence[s + 2] & 0xf0) >> 4;
  if (bit_rate == 15) {
    return false;
  }

  // Let sample-rate be sequence[s + 2] & 0x0c >> 2
  uint8_t const sample_rate = (sequence[s + 2] & 0x0c) >> 2;
  if (sample_rate == 3) {
    return false;
  }

  // Let final-layer be the result of 4 - (sequence[s + 1])
  // WHATWG spec: use entire byte, not just layer field
  uint8_t const final_layer = 4 - sequence[s + 1];
  // If final-layer & 0x06 >> 1 is not 3, return false
  if (((final_layer & 0x06) >> 1) != 3) {
    return false;
  }

  return true;
}

// WHATWG MIME Sniffing Standard - MP3 without ID3 signature algorithm
static bool match_mp3_signature(uint8_t const *const data, size_t const len) {
  // Initialize s to 0
  size_t s = 0;

  // If the result of match mp3 header is false, return false
  if (!match_mp3_header(data, len, s)) {
    return false;
  }

  // Parse an mp3 frame
  uint8_t version;
  uint32_t bitrate, freq;
  uint8_t pad;
  parse_mp3_frame(data, s, &version, &bitrate, &freq, &pad);

  // Extract layer for frame size computation
  uint8_t const layer = (data[s + 1] & 0x06) >> 1;

  // Let skipped-bytes be the return value of mp3 framesize computation
  uint32_t const skipped_bytes = compute_mp3_frame_size(version, layer, bitrate, freq, pad);

  // If skipped-bytes is less than 4, or skipped-bytes is greater than s - length, return false
  if (skipped_bytes < 4 || skipped_bytes > len - s) {
    return false;
  }

  // Increment s by skipped-bytes
  s += skipped_bytes;

  // If the result of match mp3 header operation is false, return false, else return true
  return match_mp3_header(data, len, s);
}

// Skip whitespace bytes according to WHATWG spec
static size_t skip_whitespace_bytes(uint8_t const *const data, size_t const len, size_t const start) {
  size_t pos = start;
  while (pos < len) {
    uint8_t const byte = data[pos];
    if (byte == 0x09 || byte == 0x0a || byte == 0x0c || byte == 0x0d || byte == 0x20) {
      pos++;
    } else {
      break;
    }
  }
  return pos;
}

// Check if byte is tag-terminating byte (SP or >)
static bool is_tag_terminating_byte(uint8_t const b) { return b == 0x20 || b == 0x3e; }

// Case-insensitive pattern match with tag termination check
static bool match_html_tag_pattern(uint8_t const *const data,
                                   size_t const len,
                                   size_t const start,
                                   char const *const pattern,
                                   size_t const pattern_len) {
  // Need space for pattern
  if (start + pattern_len > len) {
    return false;
  }

  // Match pattern case-insensitively
  for (size_t i = 0; i < pattern_len; i++) {
    uint8_t data_byte = data[start + i];
    uint8_t pattern_byte = (uint8_t)pattern[i];

    // Convert to lowercase for comparison
    if (data_byte >= 'A' && data_byte <= 'Z') {
      data_byte += 32;
    }
    if (pattern_byte >= 'A' && pattern_byte <= 'Z') {
      pattern_byte += 32;
    }

    if (data_byte != pattern_byte) {
      return false;
    }
  }

  // WHATWG spec: pattern must be followed by a tag-terminating byte
  // Check if there's a byte after the pattern and if it's tag-terminating
  if (start + pattern_len < len) {
    return is_tag_terminating_byte(data[start + pattern_len]);
  }

  // If no byte after pattern, it can't be followed by tag-terminating byte
  return false;
}

// Check for HTML patterns according to WHATWG spec
static bool match_html_patterns(uint8_t const *const data, size_t const len) {
  size_t const pos = skip_whitespace_bytes(data, len, 0);
  if (pos >= len) {
    return false;
  }
  // HTML tag patterns from WHATWG MIME Sniffing Standard
  if (match_html_tag_pattern(data, len, pos, "<!DOCTYPE HTML", 14)) {
    return true;
  }
  if (match_html_tag_pattern(data, len, pos, "<HTML", 5)) {
    return true;
  }
  if (match_html_tag_pattern(data, len, pos, "<HEAD", 5)) {
    return true;
  }
  if (match_html_tag_pattern(data, len, pos, "<SCRIPT", 7)) {
    return true;
  }
  if (match_html_tag_pattern(data, len, pos, "<IFRAME", 7)) {
    return true;
  }
  if (match_html_tag_pattern(data, len, pos, "<H1", 3)) {
    return true;
  }
  if (match_html_tag_pattern(data, len, pos, "<DIV", 4)) {
    return true;
  }
  if (match_html_tag_pattern(data, len, pos, "<FONT", 5)) {
    return true;
  }
  if (match_html_tag_pattern(data, len, pos, "<TABLE", 6)) {
    return true;
  }
  if (match_html_tag_pattern(data, len, pos, "<A", 2)) {
    return true;
  }
  if (match_html_tag_pattern(data, len, pos, "<STYLE", 6)) {
    return true;
  }
  if (match_html_tag_pattern(data, len, pos, "<TITLE", 6)) {
    return true;
  }
  if (match_html_tag_pattern(data, len, pos, "<B", 2)) {
    return true;
  }
  if (match_html_tag_pattern(data, len, pos, "<BODY", 5)) {
    return true;
  }
  if (match_html_tag_pattern(data, len, pos, "<BR", 3)) {
    return true;
  }
  if (match_html_tag_pattern(data, len, pos, "<P", 2)) {
    return true;
  }
  if (match_html_tag_pattern(data, len, pos, "<!--", 4)) {
    return true;
  }
  return false;
}

bool gcmz_sniff(void const *const data, size_t const len, wchar_t const **const mime, wchar_t const **const ext) {
  if (!data) {
    return false;
  }

  wchar_t const *ext_ = NULL;
  wchar_t const *mime_ = NULL;
  uint8_t const *const b = (uint8_t const *)data;
  if (len >= 6 && b[0] == 'G' && b[1] == 'I' && b[2] == 'F' && b[3] == '8' && (b[4] == '7' || b[4] == '9') &&
      b[5] == 'a') {
    ext_ = L".gif";
    mime_ = L"image/gif";
  } else if (len >= 3 && b[0] == 0xff && b[1] == 0xd8 && b[2] == 0xff) {
    ext_ = L".jpg";
    mime_ = L"image/jpeg";
  } else if (len >= 8 && b[0] == 0x89 && b[1] == 'P' && b[2] == 'N' && b[3] == 'G' && b[4] == 0x0d && b[5] == 0x0a &&
             b[6] == 0x1a && b[7] == 0x0a) {
    ext_ = L".png";
    mime_ = L"image/png";
  } else if (len >= 12 && b[0] == 'R' && b[1] == 'I' && b[2] == 'F' && b[3] == 'F' && b[8] == 'W' && b[9] == 'E' &&
             b[10] == 'B' && b[11] == 'P') {
    ext_ = L".webp";
    mime_ = L"image/webp";
  } else if (len >= 4 && b[0] == 0x00 && b[1] == 0x00 && b[2] == 0x01 && b[3] == 0x00) {
    ext_ = L".ico";
    mime_ = L"image/x-icon";
  } else if (len >= 4 && b[0] == 0x00 && b[1] == 0x00 && b[2] == 0x02 && b[3] == 0x00) {
    ext_ = L".cur";
    mime_ = L"image/x-icon";
  } else if (len >= 2 && b[0] == 'B' && b[1] == 'M') {
    ext_ = L".bmp";
    mime_ = L"image/bmp";
  } else if (len >= 12 && b[0] == 'F' && b[1] == 'O' && b[2] == 'R' && b[3] == 'M' && b[8] == 'A' && b[9] == 'I' &&
             b[10] == 'F' && b[11] == 'F') {
    ext_ = L".aiff";
    mime_ = L"audio/aiff";
  } else if (len >= 3 && b[0] == 0x49 && b[1] == 0x44 && b[2] == 0x33) {
    ext_ = L".mp3";
    mime_ = L"audio/mpeg";
  } else if (match_mp4_signature(b, len)) {
    ext_ = L".mp4";
    mime_ = L"video/mp4";
  } else if (match_webm_signature(b, len)) {
    ext_ = L".webm";
    mime_ = L"video/webm";
  } else if (match_mp3_signature(b, len)) {
    ext_ = L".mp3";
    mime_ = L"audio/mpeg";
  } else if (len >= 5 && b[0] == 'O' && b[1] == 'g' && b[2] == 'g' && b[3] == 'S' && b[4] == 0x00) {
    ext_ = L".ogg";
    mime_ = L"application/ogg";
  } else if (len >= 8 && b[0] == 'M' && b[1] == 'T' && b[2] == 'h' && b[3] == 'd' && b[4] == 0x00 && b[5] == 0x00 &&
             b[6] == 0x00 && b[7] == 0x06) {
    ext_ = L".mid";
    mime_ = L"audio/midi";
  } else if (len >= 12 && b[0] == 'R' && b[1] == 'I' && b[2] == 'F' && b[3] == 'F' && b[8] == 'A' && b[9] == 'V' &&
             b[10] == 'I' && b[11] == ' ') {
    ext_ = L".avi";
    mime_ = L"video/avi";
  } else if (len >= 12 && b[0] == 'R' && b[1] == 'I' && b[2] == 'F' && b[3] == 'F' && b[8] == 'W' && b[9] == 'A' &&
             b[10] == 'V' && b[11] == 'E') {
    ext_ = L".wav";
    mime_ = L"audio/wave";
  } else if (len >= 5 && b[0] == '%' && b[1] == 'P' && b[2] == 'D' && b[3] == 'F' && b[4] == '-') {
    ext_ = L".pdf";
    mime_ = L"application/pdf";
  } else if (match_html_patterns(b, len)) {
    ext_ = L".html";
    mime_ = L"text/html";
  } else if (len >= 5 && b[0] == '<' && b[1] == '?' && b[2] == 'x' && b[3] == 'm' && b[4] == 'l') {
    // "<?xml" - XML declaration
    ext_ = L".xml";
    mime_ = L"text/xml";
  } else if (len >= 11 && b[0] == '%' && b[1] == '!' && b[2] == 'P' && b[3] == 'S' && b[4] == '-' && b[5] == 'A' &&
             b[6] == 'd' && b[7] == 'o' && b[8] == 'b' && b[9] == 'e' && b[10] == '-') {
    // "%!PS-Adobe-" - PostScript signature
    ext_ = L".ps";
    mime_ = L"application/postscript";
  } else if (len >= 3 && b[0] == 0x1f && b[1] == 0x8b && b[2] == 0x08) {
    // GZIP archive signature
    ext_ = L".gz";
    mime_ = L"application/x-gzip";
  } else if (len >= 4 && b[0] == 'P' && b[1] == 'K' && b[2] == 0x03 && b[3] == 0x04) {
    // "PK" followed by ETX EOT - ZIP archive signature
    ext_ = L".zip";
    mime_ = L"application/zip";
  } else if (len >= 7 && b[0] == 'R' && b[1] == 'a' && b[2] == 'r' && b[3] == '!' && b[4] == 0x1a && b[5] == 0x07 &&
             b[6] == 0x00) {
    // "Rar!" followed by SUB BEL NUL - RAR 4.x archive signature
    ext_ = L".rar";
    mime_ = L"application/x-rar-compressed";
  } else if (len >= 36 && b[34] == 'L' && b[35] == 'P') {
    // 34 bytes followed by "LP" - Embedded OpenType signature
    ext_ = L".eot";
    mime_ = L"application/vnd.ms-fontobject";
  } else if (len >= 4 && b[0] == 0x00 && b[1] == 0x01 && b[2] == 0x00 && b[3] == 0x00) {
    // 4 bytes representing version number 1.0 - TrueType signature
    ext_ = L".ttf";
    mime_ = L"font/ttf";
  } else if (len >= 4 && b[0] == 'O' && b[1] == 'T' && b[2] == 'T' && b[3] == 'O') {
    // "OTTO" - OpenType signature
    ext_ = L".otf";
    mime_ = L"font/otf";
  } else if (len >= 4 && b[0] == 't' && b[1] == 't' && b[2] == 'c' && b[3] == 'f') {
    // "ttcf" - TrueType Collection signature
    ext_ = L".ttc";
    mime_ = L"font/collection";
  } else if (len >= 4 && b[0] == 'w' && b[1] == 'O' && b[2] == 'F' && b[3] == 'F') {
    // "wOFF" - Web Open Font Format 1.0 signature
    ext_ = L".woff";
    mime_ = L"font/woff";
  } else if (len >= 4 && b[0] == 'w' && b[1] == 'O' && b[2] == 'F' && b[3] == '2') {
    // "wOF2" - Web Open Font Format 2.0 signature
    ext_ = L".woff2";
    mime_ = L"font/woff2";
  } else if (len >= 2 && b[0] == 0xfe && b[1] == 0xff) {
    // UTF-16BE BOM
    ext_ = L".txt";
    mime_ = L"text/plain";
  } else if (len >= 2 && b[0] == 0xff && b[1] == 0xfe) {
    // UTF-16LE BOM
    ext_ = L".txt";
    mime_ = L"text/plain";
  } else if (len >= 3 && b[0] == 0xef && b[1] == 0xbb && b[2] == 0xbf) {
    // UTF-8 BOM
    ext_ = L".txt";
    mime_ = L"text/plain";
  } else {
    ext_ = L".bin";
    mime_ = L"application/octet-stream";
  }

  if (mime) {
    *mime = mime_;
  }
  if (ext) {
    *ext = ext_;
  }
  return true;
}
