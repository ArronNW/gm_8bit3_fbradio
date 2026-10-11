#pragma once
#include "opus.h"
#include "ivoicecodec.h"
#include <cstddef>   // для ptrdiff_t
#include <algorithm>
#include <deque>
#include <vector>

namespace SteamOpus {

#define SAMPLERATE_GMOD_OPUS 24000
#define FRAME_SIZE_GMOD 480

    enum opcodes {
        OP_CODEC_OPUSPLC = 6,
        OP_SAMPLERATE = 11,
        OP_SILENCE = 0
    };

    class Opus_FrameDecoder : public IVoiceCodec {
    private:
        // Запрет копирования
        Opus_FrameDecoder(const Opus_FrameDecoder&);
        Opus_FrameDecoder& operator=(const Opus_FrameDecoder&);

    public:
        Opus_FrameDecoder();
        virtual ~Opus_FrameDecoder();

        virtual bool Init(int quality, int sampleRate);
        virtual int GetSampleRate();
        virtual bool ResetState();
        virtual void Release();
        virtual int Compress(const char* pUncompressed, int nSamples, char* pCompressed, int maxCompressedBytes, bool bFinal);
        virtual int Decompress(const char* pCompressed, int compressedBytes, char* pUncompressed, int maxUncompressedBytes);

    private:
        uint16_t m_seq;
        uint16_t m_encodeSeq;
        OpusDecoder* dec;
        OpusEncoder* enc;
        std::deque<uint16_t> sample_buf;
    };

    // Реализация методов (можно оставить в этом же файле, если это .h, но лучше перенести в .cpp)
    inline Opus_FrameDecoder::Opus_FrameDecoder() : m_seq(0), m_encodeSeq(0), dec(0), enc(0) {
        int error = 0;
        dec = opus_decoder_create(SAMPLERATE_GMOD_OPUS, 1, &error);
        enc = opus_encoder_create(SAMPLERATE_GMOD_OPUS, 1, OPUS_APPLICATION_VOIP, &error);
    }

    inline Opus_FrameDecoder::~Opus_FrameDecoder() {
        if (dec) opus_decoder_destroy(dec);
        if (enc) opus_encoder_destroy(enc);
    }

    inline bool Opus_FrameDecoder::Init(int /*quality*/, int /*sampleRate*/) {
        return true;
    }

    inline int Opus_FrameDecoder::GetSampleRate() {
        return SAMPLERATE_GMOD_OPUS;
    }

    inline bool Opus_FrameDecoder::ResetState() {
        opus_decoder_ctl(dec, OPUS_RESET_STATE);
        opus_encoder_ctl(enc, OPUS_RESET_STATE);
        return true;
    }

    inline void Opus_FrameDecoder::Release() {}

    inline int Opus_FrameDecoder::Compress(const char* pUncompressed, int nSamples, char* pCompressed, int maxCompressedBytes, bool bFinal) {
        if (nSamples <= 0) return 0;

        const char* const pCompressedBase = pCompressed;
        char* pCompressedEnd = pCompressed + maxCompressedBytes;

        // Если недостаточно сэмплов для полного фрейма, сохраняем в буфер
        if (sample_buf.size() + static_cast<size_t>(nSamples) < FRAME_SIZE_GMOD && !bFinal) {
            const uint16_t* src = reinterpret_cast<const uint16_t*>(pUncompressed);
            sample_buf.insert(sample_buf.end(), src, src + nSamples);
            return 0;
        }

        // Собираем все сэмплы во временный буфер
        std::vector<uint16_t> temp_buf;
        temp_buf.reserve(sample_buf.size() + nSamples + FRAME_SIZE_GMOD);
        temp_buf.insert(temp_buf.end(), sample_buf.begin(), sample_buf.end());
        sample_buf.clear();

        const uint16_t* src = reinterpret_cast<const uint16_t*>(pUncompressed);
        temp_buf.insert(temp_buf.end(), src, src + nSamples);

        size_t remainder = temp_buf.size() % FRAME_SIZE_GMOD;
        if (remainder != 0) {
            if (bFinal) {
                // Добиваем нулями до полного фрейма
                size_t pad = FRAME_SIZE_GMOD - remainder;
                temp_buf.insert(temp_buf.end(), pad, 0);
            }
            else {
                // Сохраняем остаток в sample_buf и удаляем из temp_buf
                size_t to_keep = remainder;
                sample_buf.insert(sample_buf.end(), temp_buf.end() - to_keep, temp_buf.end());
                temp_buf.erase(temp_buf.end() - to_keep, temp_buf.end());
            }
        }

        // Кодируем все полные фреймы
        for (size_t i = 0; i < temp_buf.size(); i += FRAME_SIZE_GMOD) {
            const uint16_t* chunk = &temp_buf[i];

            // Место для длины фрейма
            if (pCompressed + static_cast<ptrdiff_t>(sizeof(uint16_t)) > pCompressedEnd)
                return -1;
            uint16_t* chunk_len = reinterpret_cast<uint16_t*>(pCompressed);
            pCompressed += sizeof(uint16_t);

            // Место для seq номера
            if (pCompressed + static_cast<ptrdiff_t>(sizeof(uint16_t)) > pCompressedEnd)
                return -1;
            *reinterpret_cast<uint16_t*>(pCompressed) = m_encodeSeq++;
            pCompressed += sizeof(uint16_t);

            // Кодируем
            ptrdiff_t remaining = pCompressedEnd - pCompressed;
            int bytes_to_write = (remaining < 0x7FFF) ? static_cast<int>(remaining) : 0x7FFF;
            int bytes_written = opus_encode(enc, reinterpret_cast<const opus_int16*>(chunk), FRAME_SIZE_GMOD,
                reinterpret_cast<unsigned char*>(pCompressed), bytes_to_write);
            if (bytes_written < 0)
                return -1;

            *chunk_len = static_cast<uint16_t>(bytes_written);
            pCompressed += bytes_written;
        }

        if (bFinal) {
            opus_encoder_ctl(enc, OPUS_RESET_STATE);
            m_encodeSeq = 0;
            // Пишем маркер конца
            if (pCompressed + static_cast<ptrdiff_t>(sizeof(uint16_t)) > pCompressedEnd)
                return -1;
            *reinterpret_cast<uint16_t*>(pCompressed) = 0xFFFF;
            pCompressed += sizeof(uint16_t);
        }

        return static_cast<int>(pCompressed - pCompressedBase);
    }

    inline int Opus_FrameDecoder::Decompress(const char* pCompressed, int compressedBytes, char* pUncompressed, int maxUncompressedBytes) {
        const char* const pUncompressedOrig = pUncompressed;
        const char* const pEnd = pCompressed + compressedBytes;
        const char* const pUncompressedEnd = pUncompressed + maxUncompressedBytes;

        while (pCompressed + static_cast<ptrdiff_t>(sizeof(uint16_t)) <= pEnd) {
            // Читаем длину
            if (pCompressed + static_cast<ptrdiff_t>(sizeof(uint16_t)) > pEnd)
                return -1;
            uint16_t len = *reinterpret_cast<const uint16_t*>(pCompressed);
            pCompressed += sizeof(uint16_t);

            if (len == 0xFFFF) {
                opus_decoder_ctl(dec, OPUS_RESET_STATE);
                m_seq = 0;
                continue;
            }

            // Читаем seq
            if (pCompressed + static_cast<ptrdiff_t>(sizeof(uint16_t)) > pEnd)
                return -1;
            uint16_t seq = *reinterpret_cast<const uint16_t*>(pCompressed);
            pCompressed += sizeof(uint16_t);

            if (seq < m_seq) {
                opus_decoder_ctl(dec, OPUS_RESET_STATE);
            }
            else if (seq > m_seq) {
                uint32_t lostFrames = seq - m_seq;
                if (lostFrames > 10) lostFrames = 10;
                for (uint32_t i = 0; i < lostFrames; ++i) {
                    if (pUncompressedEnd - pUncompressed <= 0)
                        return -1;
                    int samples = opus_decode(dec, 0, 0, reinterpret_cast<opus_int16*>(pUncompressed),
                        static_cast<int>((pUncompressedEnd - pUncompressed) / 2), 0);
                    if (samples < 0)
                        break;
                    pUncompressed += samples * 2;
                }
                m_seq = seq;
            }

            m_seq = seq + 1;

            if (len == 0 || pCompressed + len > pEnd)
                return -1;

            int samples = opus_decode(dec, reinterpret_cast<const unsigned char*>(pCompressed), len,
                reinterpret_cast<opus_int16*>(pUncompressed),
                static_cast<int>((pUncompressedEnd - pUncompressed) / 2), 0);
            if (samples < 0)
                return -1;

            pUncompressed += samples * 2;
            pCompressed += len;
        }

        return static_cast<int>((pUncompressed - pUncompressedOrig) / sizeof(uint16_t));
    }

} // namespace SteamOpus