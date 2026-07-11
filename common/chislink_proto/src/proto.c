#include "chislink/proto.h"

bool clp_decode_header_words(const uint32_t words[4], clp_header_t *out) {
    if (!words || !out || !clp_is_protocol_word(words[0])) {
        return false;
    }

    out->type = clp_word_type(words[0]);
    out->channel = clp_word_channel(words[0]);
    out->opcode = clp_word_opcode(words[0]);
    out->imm = clp_word_imm(words[0]);
    out->length = words[1];
    out->seq = clp_seq_from_word(words[2]);
    out->flags = clp_flags_from_word(words[2]);
    out->crc32 = words[3];
    return true;
}

void clp_encode_header_words(const clp_header_t *header, uint32_t words[4]) {
    if (!header || !words) {
        return;
    }

    words[0] = clp_make_word(header->type, header->channel,
                             header->opcode, header->imm);
    words[1] = header->length;
    words[2] = clp_make_seq_flags(header->seq, header->flags);
    words[3] = header->crc32;
}

size_t clp_aligned_length(size_t length) {
    return (length + 3u) & ~(size_t)3u;
}

bool clp_is_protocol_word(uint32_t word) {
    return clp_word_version(word) == CLP_VERSION;
}
