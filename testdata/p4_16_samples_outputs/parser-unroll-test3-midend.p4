#include <core.p4>
#define V1MODEL_VERSION 20180101
#include <v1model.p4>

header ethernet_t {
    bit<48> dstAddr;
    bit<48> srcAddr;
    bit<16> etherType;
}

header srcRoute_t {
    bit<2>  bos;
    bit<15> port;
}

header ipv4_t {
    bit<4>  version;
    bit<4>  ihl;
    bit<8>  diffserv;
    bit<16> totalLen;
    bit<16> identification;
    bit<3>  flags;
    bit<13> fragOffset;
    bit<8>  ttl;
    bit<8>  protocol;
    bit<16> hdrChecksum;
    bit<32> srcAddr;
    bit<32> dstAddr;
}

struct metadata {
}

struct headers {
    ethernet_t       ethernet;
    srcRoute_t[16w4] srcRoutes;
    ipv4_t           ipv4;
    bit<32>          index;
}

parser MyParser(packet_in packet, out headers hdr, inout metadata meta, inout standard_metadata_t standard_metadata) {
    state start {
        hdr.index = 32w0;
        packet.extract<ethernet_t>(hdr.ethernet);
        transition select(hdr.ethernet.etherType) {
            16w0x1234: parse_srcRouting;
            default: accept;
        }
    }
    state parse_srcRouting {
        packet.extract<srcRoute_t>(hdr.srcRoutes.next);
        transition select(hdr.srcRoutes.last.bos) {
            2w1: parse_ipv4;
            2w2: callMidle;
            default: callMidle;
        }
    }
    state callMidle {
        hdr.index = hdr.index + 32w1;
        packet.extract<srcRoute_t>(hdr.srcRoutes.next);
        transition select(hdr.srcRoutes.last.bos) {
            2w1: parse_ipv4;
            default: parse_srcRouting;
        }
    }
    state parse_ipv4 {
        packet.extract<ipv4_t>(hdr.ipv4);
        transition accept;
    }
}

control mau(inout headers hdr, inout metadata meta, inout standard_metadata_t sm) {
    apply {
    }
}

control deparse(packet_out pkt, in headers hdr) {
    apply {
    }
}

control verifyChecksum(inout headers hdr, inout metadata meta) {
    apply {
    }
}

control computeChecksum(inout headers hdr, inout metadata meta) {
    apply {
    }
}

V1Switch<headers, metadata>(MyParser(), verifyChecksum(), mau(), mau(), computeChecksum(), deparse()) main;

