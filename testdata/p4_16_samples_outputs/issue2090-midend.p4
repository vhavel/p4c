#include <core.p4>

header Mpls_h {
    bit<20> label;
    bit<3>  tc;
    bit<1>  bos;
    bit<8>  ttl;
}

header Tcp_option_end_h {
    bit<8> kind;
}

header Tcp_option_nop_h {
    bit<8> kind;
}

header Tcp_option_ss_h {
    bit<8>  kind;
    bit<32> maxSegmentSize;
}

header Tcp_option_s_h {
    bit<8>  kind;
    bit<24> scale;
}

header Tcp_option_sack_h {
    bit<8>      kind;
    bit<8>      length;
    varbit<256> sack;
}

header_union Tcp_option_h {
    Tcp_option_end_h  end;
    Tcp_option_nop_h  nop;
    Tcp_option_ss_h   ss;
    Tcp_option_s_h    s;
    Tcp_option_sack_h sack;
}

struct Tcp_option_sack_top {
    int<8> kind;
    bit<8> length;
    bool   f;
    bit<7> padding;
}

parser Tcp_option_parser(packet_in b, out Tcp_option_h[10] vec) {
    @name("Tcp_option_parser.tmp_0") bit<8> tmp_0;
    bit<24> tmp_5;
    state start {
        tmp_0 = b.lookahead<bit<8>>();
        transition select(tmp_0) {
            8w0x0: parse_tcp_option_end;
            8w0x1: parse_tcp_option_nop;
            8w0x2: parse_tcp_option_ss;
            8w0x3: parse_tcp_option_s;
            8w0x5: parse_tcp_option_sack;
            default: noMatch;
        }
    }
    state parse_tcp_option_end {
        b.extract<Tcp_option_end_h>(vec.next.end);
        transition accept;
    }
    state parse_tcp_option_nop {
        b.extract<Tcp_option_nop_h>(vec.next.nop);
        transition start;
    }
    state parse_tcp_option_ss {
        b.extract<Tcp_option_ss_h>(vec.next.ss);
        transition start;
    }
    state parse_tcp_option_s {
        b.extract<Tcp_option_s_h>(vec.next.s);
        transition start;
    }
    state parse_tcp_option_sack {
        tmp_5 = b.lookahead<bit<24>>();
        b.extract<Tcp_option_sack_h>(vec.next.sack, (bit<32>)((tmp_5[15:8] << 3) + 8w240));
        transition start;
    }
    state noMatch {
        verify(false, error.NoMatch);
        transition reject;
    }
}

parser pr<H>(packet_in b, out H h);
package top<H>(pr<H> p);
top<Tcp_option_h[10]>(Tcp_option_parser()) main;

