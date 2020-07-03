#include <cstdint>
#include <cstdio>
#include <vector>
#include "lz77.h"

static std::vector<uint8_t> compress_gba_lz77_rev(const std::vector<uint8_t> &input, int flags, int max_scenarios) {
	std::vector<uint8_t> output;
	// Before anything, validate the size can be supported.
	if (input.size() > 0x00FFFFFF) {
		fprintf(stderr, "Input data too big to compress via LZ77.\n");
		return output;
	}

	std::vector<std::vector<uint8_t>> chunks;
	// This is the worst case.
	chunks.reserve(input.size());

	constexpr int MAX_BACKREF_OFFSET = 0xFFF;
	constexpr int MIN_BACKREF_LEN = 3;
	constexpr int MAX_BACKREF_LEN = 3 + 0xF;

	struct Scenario {
		int waste = 1;
		int backref_offset = 0;
		int backref_len = 0;
		int saved_bytes = 0;
	};

	auto try_scenario = [&](size_t src, int waste) {
		Scenario plan{ waste };
		// Start out in debt, our match has to save more.
		plan.saved_bytes = -2 - waste;
		if ((size_t)waste + MIN_BACKREF_LEN >= src) {
			return plan;
		}

		// According to this plan, our buffer will END at this position.
		src -= waste;

		// We have at most this many bytes to encode.
		int left = src - MIN_BACKREF_LEN > (size_t)MAX_BACKREF_LEN ? MAX_BACKREF_LEN : (int)src - MIN_BACKREF_LEN;
		if (left < MIN_BACKREF_LEN) {
			return plan;
		}

		// This is the farthest back we could possibly go.
		int farthest_offset = src - MIN_BACKREF_LEN > (size_t)MAX_BACKREF_OFFSET ? MAX_BACKREF_OFFSET : (int)(src - MIN_BACKREF_LEN);
		int best_offset = 0;
		int best_len = 0;

		for (int off = farthest_offset; off > 1; ) {
			// Find the next match after this.  Note: this is the last byte of the backref.
			uint8_t *match = (uint8_t *)memchr(input.data() + src - off, input[src - 1], off - 1);
			if (!match) {
				break;
			}

			// Okay, found one.  Set the next search offset after it in case we reject.
			size_t found_pos = match - input.data();
			int found_off = -(int)(found_pos - src);
			off = found_off - 1;

			// How long is this match?  We only looked for the last byte so far.
			int found_len = 1;
			int max_len = found_pos >= (size_t)left ? left : (int)found_pos;
			for (int j = 1; j < max_len; ++j) {
				if (input[found_pos - j] != input[src - j - 1]) {
					break;
				}
				found_len++;
			}

			// Too short, rejected.  Look for another end byte.
			if (found_len < MIN_BACKREF_LEN || found_len < best_len) {
				continue;
			}
			if ((flags & LZ77_VRAM_SAFE) != 0 && found_off - 2 <= 0) {
				continue;
			}

			// Maybe this is the one?
			best_len = found_len;
			best_offset = found_off;
		}

		if (best_len >= MIN_BACKREF_LEN) {
			// Alright, this is our best shot.
			plan.backref_offset = best_offset - 2;
			plan.backref_len = best_len;
			plan.saved_bytes += best_len;
		}
		return plan;
	};

	// Start at the END, and build the file backwards.
	// This gives us the smartest backreference decisions.
	ptrdiff_t total_saved = 0;
	size_t compressed_size = 0;
	for (size_t src = input.size(); src > 0; ) {
		for (int i = 0; i < 8; ++i) {
			if (src == 0 || src > input.size()) {
				break;
			}

			// Default plan is no backref.
			Scenario plan;

			// Play Chess.  Is it smarter to waste a byte now to save one later?
			for (int s = 0; s < max_scenarios; ++s) {
				Scenario option = try_scenario(src, s);
				if (option.saved_bytes > plan.saved_bytes) {
					// TODO: Could cache these for speed?
					plan = option;
					// If any plan wasting a byte is better, go with it.
					// We don't need to examine future plans, since we'll do this next byte.
					if (s != 0) {
						break;
					}
					// If this is already ideal, we're done.
					if (plan.backref_len == MAX_BACKREF_LEN) {
						break;
					}
				} else if (s == 0) {
					// Won't save, no sense checking other scenarios.
					break;
				}
			}

			if (plan.waste != 0) {
				chunks.push_back({ input[--src] });
				compressed_size++;
			} else {
				uint8_t byte1 = ((plan.backref_len - 3) << 4) | (plan.backref_offset >> 8);
				uint8_t byte2 = plan.backref_offset & 0xFF;
				chunks.push_back({ byte1, byte2 });
				src -= plan.backref_len;
				total_saved += plan.saved_bytes;
				compressed_size += 2;
			}
		}
	}

	compressed_size += (chunks.size() + 7) / 8;
	compressed_size += chunks.size() & 7;

	output.resize((compressed_size + 4 + 3) & ~3);
	output[0] = 0x10;
	output[1] = (input.size() >> 0) & 0xFF;
	output[2] = (input.size() >> 8) & 0xFF;
	output[3] = (input.size() >> 16) & 0xFF;

	// Time to reverse the chunks and insert the flags.
	size_t pos = 4;
	for (size_t i = 0; i < chunks.size(); i += 8) {
		uint8_t *quadflags = &output[pos++];
		for (int j = 0; j < 8 && i + j < chunks.size(); ++j) {
			const auto &chunk = chunks[chunks.size() - i - j - 1];
			if (chunk.size() == 1) {
				output[pos++] = chunk[0];
			} else {
				*quadflags |= 0x80 >> j;
				output[pos++] = chunk[0];
				output[pos++] = chunk[1];
			}
		}
	}

	return output;
}

static std::vector<uint8_t> compress_gba_lz77_fwd(const std::vector<uint8_t> &input, int flags, int max_scenarios) {
	std::vector<uint8_t> output;
	// Before anything, validate the size can be supported.
	if (input.size() > 0x00FFFFFF) {
		fprintf(stderr, "Input data too big to compress via LZ77.\n");
		return output;
	}

	// Worst case: we add one extra byte for each 8, plus 4 byte header.
	output.resize(input.size() + (input.size() + 7) / 8 + 4);
	uint8_t *pos = output.data();

	// Now the header, method 0x10 + size << 4, in LE.
	*pos++ = 0x10;
	*pos++ = (input.size() >> 0) & 0xFF;
	*pos++ = (input.size() >> 8) & 0xFF;
	*pos++ = (input.size() >> 16) & 0xFF;

	constexpr int MAX_BACKREF_OFFSET = 0xFFF;
	constexpr int MIN_BACKREF_LEN = 3;
	constexpr int MAX_BACKREF_LEN = 3 + 0xF;

	struct Scenario {
		int waste = 1;
		int backref_offset = 0;
		int backref_len = 0;
		int saved_bytes = 0;
	};

	auto try_scenario = [&](size_t src, int waste) {
		Scenario plan{ waste };
		// Start out in debt, our match has to save more.
		plan.saved_bytes = -2 - waste;
		// According to this plan, we'll have this many src bytes to match.
		src += waste;

		if (src == 0 || src + waste >= input.size()) {
			return plan;
		}

		// We have at most this many bytes to encode.
		int left = input.size() - src > (size_t)MAX_BACKREF_LEN ? MAX_BACKREF_LEN : (int)(input.size() - src);
		if (left < MIN_BACKREF_LEN) {
			return plan;
		}

		// This is as far back as we can go.
		int base_offset = src <= (size_t)MAX_BACKREF_OFFSET ? (int)src : MAX_BACKREF_OFFSET + 1;
		int best_offset = 0;
		int best_len = 0;

		// Doesn't matter forwards or backwards since it's a fixed encoding.
		for (int off = base_offset; off > 0; ) {
			// Find the next match after this.
			uint8_t *match = (uint8_t *)memchr(input.data() + src - off, input[src], off);
			if (!match) {
				break;
			}

			// Okay, found one.  Set the next search offset after it in case we reject.
			size_t found_pos = match - input.data();
			int found_off = -(int)(found_pos - src);
			off = found_off - 1;

			// How long is this match?  We only looked for the start byte so far.
			int found_len = 1;
			for (int j = 1; j < left; ++j) {
				if (match[j] != input[src + j]) {
					break;
				}
				found_len++;
			}

			// Too short, rejected.  Look for another start byte.
			if (found_len < MIN_BACKREF_LEN || found_len < best_len) {
				continue;
			}
			if ((flags & LZ77_VRAM_SAFE) != 0 && found_off - 1 <= 0) {
				continue;
			}

			// Maybe this is the one?
			best_len = found_len;
			best_offset = found_off;
		}

		if (best_len >= MIN_BACKREF_LEN) {
			// Alright, this is our best shot.
			plan.backref_offset = best_offset - 1;
			plan.backref_len = best_len;
			plan.saved_bytes += best_len;
		}
		return plan;
	};

	ptrdiff_t total_saved = 0;
	for (size_t src = 0; src < input.size(); ) {
		uint8_t *quadflags = pos++;
		uint8_t nowflags = 0;

		for (int i = 0; i < 8; ++i) {
			if (src >= input.size()) {
				*pos++ = 0;
				continue;
			}

			// Default plan is no backref.
			Scenario plan;

			// Play Chess.  Is it smarter to waste a byte now to save one later?
			for (int s = 0; s < max_scenarios; ++s) {
				Scenario option = try_scenario(src, s);
				if (option.saved_bytes >= plan.saved_bytes) {
					// TODO: Could cache these for speed?
					plan = option;
					// If any plan wasting a byte is better, go with it.
					// We don't need to examine future plans, since we'll do this next byte.
					if (s != 0) {
						break;
					}
					// This is good enough, let's not keep looking.  Might be worse.
					if (plan.backref_len >= 13) {
						break;
					}
				} else if (s == 0) {
					// Won't save, no sense checking other scenarios.
					break;
				}
			}

			if (plan.waste != 0) {
				*pos++ = input[src++];
			} else {
				*pos++ = ((plan.backref_len - 3) << 4) | (plan.backref_offset >> 8);
				*pos++ = plan.backref_offset & 0xFF;
				src += plan.backref_len;
				nowflags |= 0x80 >> i;
				total_saved += plan.saved_bytes;
			}
		}

		*quadflags = nowflags;
		total_saved--;
	}

	size_t len = pos - output.data();
	output.resize((len + 3) & ~3);
	return output;
}

std::vector<uint8_t> compress_gba_lz77(const std::vector<uint8_t> &input, int flags) {
	if (flags & LZ77_FAST) {
		return compress_gba_lz77_fwd(input, flags, 1);
	}

	// Try to compress as well as possible to fit more in the same space.
	std::vector<uint8_t> best;
	for (int s = 0; s < 4; ++s) {
		std::vector<uint8_t> trial = compress_gba_lz77_fwd(input, flags, s);
		if (trial.size() < best.size() || best.size() == 0) {
			best = std::move(trial);
		}

		trial = compress_gba_lz77_rev(input, flags, s);
		if (trial.size() < best.size() || best.size() == 0) {
			best = std::move(trial);
		}
	}

	return best;
}