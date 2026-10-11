#pragma once
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <ivoicecodec.h>
#include <checksum_crc.h>

namespace SteamVoice {
	enum {
		OP_SILENCE = 0,
		OP_CODEC_OPUSPLC = 6,
		OP_SAMPLERATE = 11
	};

	//Outputs bytes written or -1 on corruption
	inline int DecompressIntoBuffer(IVoiceCodec* codec, const char* compressedData, int compressedLen, char* decompressedOut, int maxDecompressed) {
		if (codec == nullptr || compressedData == nullptr || decompressedOut == nullptr ||
			compressedLen < static_cast<int>(sizeof(uint64_t) + sizeof(uint32_t)) || maxDecompressed <= 0)
			return -1;

		const char* curRead = compressedData;
		const char* maxRead = compressedData + compressedLen;
		char* curWrite = decompressedOut;
		char* maxWrite = decompressedOut + maxDecompressed;

		//Strip steamid at beginning of packet and crc at end of packet
		curRead += sizeof(uint64_t);
		maxRead -= sizeof(uint32_t);

		while (curRead < maxRead) {
			//Check to make sure we have one byte of buffer space remaining at least
			if (curRead + sizeof(char) > maxRead)
				return -1;

			//Get the current packet opcode
			char opcode = *curRead;
			curRead += sizeof(char);

			switch (opcode) {
			case OP_SILENCE: {
				//Contains a number of silence samples to add to the decompressed data. Skip for now.
				if (curRead + sizeof(uint16_t) > maxRead)
					return -1;

				curRead += sizeof(uint16_t);
				break;
			}
			case OP_SAMPLERATE: {
				//Contains the samplerate for the stream. Always 24000 as far as I can tell.
				if (curRead + sizeof(uint16_t) > maxRead)
					return -1;

				//Something like this could be used to read the sample rate if we needed it. 
				//uint16_t sampleRate = *(uint16_t*)curRead;

				curRead += sizeof(uint16_t);
				break;
			}
			case OP_CODEC_OPUSPLC: {
				//Contains length plus a number of steam opus frames
				if (curRead + sizeof(uint16_t) > maxRead)
					return -1;

				uint16_t frameDataLen = 0;
				std::memcpy(&frameDataLen, curRead, sizeof(frameDataLen));
				curRead += sizeof(uint16_t);
				if (curRead + frameDataLen > maxRead)
					return -1;

				const std::ptrdiff_t remaining = maxWrite - curWrite;
				if (remaining <= 0 || remaining > std::numeric_limits<int>::max())
					return -1;

				const int decompressedSamples = codec->Decompress(
					curRead, frameDataLen, curWrite, static_cast<int>(remaining));
				if (decompressedSamples <= 0)
					return -1;
				if (decompressedSamples > remaining / static_cast<int>(sizeof(int16_t)))
					return -1;

				curWrite += decompressedSamples * sizeof(int16_t);
				curRead += frameDataLen;
				break;
			}
			default:
				return -1;
			}
		}

		const std::ptrdiff_t written = curWrite - decompressedOut;
		return written <= std::numeric_limits<int>::max() ? static_cast<int>(written) : -1;
	}

	//Outputs number of bytes written or -1 on failure
	inline int CompressIntoBuffer(uint64_t steamid, IVoiceCodec* codec, const char* inputData, int inputLen, char* compressedOut, int maxCompressed, int sampleRate) {
		if (codec == nullptr || inputData == nullptr || compressedOut == nullptr || inputLen < 0 ||
			maxCompressed < static_cast<int>(sizeof(uint64_t) + 2 * sizeof(char) + 2 * sizeof(uint16_t) + sizeof(CRC32_t)) ||
			sampleRate < 0 || sampleRate > std::numeric_limits<uint16_t>::max())
			return -1;

		char* curWrite = compressedOut;
		char* maxWrite = compressedOut + maxCompressed;

		if (curWrite + sizeof(uint64_t) > maxWrite)
			return -1;

		std::memcpy(curWrite, &steamid, sizeof(steamid));
		curWrite += sizeof(uint64_t);

		//Write sample rate operation
		if (curWrite + sizeof(char) + sizeof(uint16_t) > maxWrite)
			return -1;

		*curWrite = OP_SAMPLERATE;
		curWrite += sizeof(char);
		const uint16_t encodedSampleRate = static_cast<uint16_t>(sampleRate);
		std::memcpy(curWrite, &encodedSampleRate, sizeof(encodedSampleRate));
		curWrite += sizeof(uint16_t);

		//Write opus codec operation
		if (curWrite + sizeof(char) + sizeof(uint16_t) > maxWrite)
			return -1;

		*curWrite = OP_CODEC_OPUSPLC;
		curWrite += sizeof(char);

		//Setup address to write to with compression length 
		char* outLenAddr = curWrite;
		curWrite += sizeof(uint16_t);

		const std::ptrdiff_t remaining = maxWrite - curWrite;
		if (remaining <= 0 || remaining > std::numeric_limits<int>::max())
			return -1;

		const int compressedBytes = codec->Compress(
			inputData, inputLen / static_cast<int>(sizeof(int16_t)), curWrite, static_cast<int>(remaining), false);

		if (compressedBytes < 0 || compressedBytes > remaining ||
			compressedBytes > std::numeric_limits<uint16_t>::max())
			return -1;

		curWrite += compressedBytes;
		const uint16_t encodedLength = static_cast<uint16_t>(compressedBytes);
		std::memcpy(outLenAddr, &encodedLength, sizeof(encodedLength));

		if (curWrite + sizeof(CRC32_t) > maxWrite)
			return -1;

		const std::ptrdiff_t payloadLength = curWrite - compressedOut;
		if (payloadLength > std::numeric_limits<int>::max())
			return -1;
		const CRC32_t crc = CRC32_ProcessSingleBuffer(compressedOut, static_cast<int>(payloadLength));
		std::memcpy(curWrite, &crc, sizeof(crc));

		curWrite += sizeof(CRC32_t);

		const std::ptrdiff_t written = curWrite - compressedOut;
		return written <= std::numeric_limits<int>::max() ? static_cast<int>(written) : -1;
	}
}
