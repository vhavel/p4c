#include <core.p4>
#include <pna.p4>

typedef bit<48> EthernetAddress;
header ethernet_t {
    EthernetAddress dstAddr;
    EthernetAddress srcAddr;
    bit<16>         etherType;
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

struct empty_metadata_t {
}

struct main_metadata_t {
}

struct headers_t {
    ethernet_t ethernet;
    ipv4_t     ipv4;
    ethernet_t ethernet1;
}

void copy_header_ethernet(inout ethernet_t hdr, in ethernet_t outer_hdr) {
    hdr.dstAddr = outer_hdr.dstAddr;
    hdr.srcAddr = outer_hdr.srcAddr;
    hdr.etherType = outer_hdr.etherType;
}
control PreControlImpl(in headers_t hdr, inout main_metadata_t meta, in pna_pre_input_metadata_t istd, inout pna_pre_output_metadata_t ostd) {
    apply {
    }
}

parser MainParserImpl(packet_in pkt, out headers_t hdr, inout main_metadata_t main_meta, in pna_main_parser_input_metadata_t istd) {
    state start {
        pkt.extract(hdr.ethernet);
        transition select(hdr.ethernet.etherType) {
            0x800: parse_ipv4;
            default: accept;
        }
    }
    state parse_ipv4 {
        pkt.extract(hdr.ipv4);
        transition accept;
    }
}

control MainControlImpl(inout headers_t hdr, inout main_metadata_t user_meta, in pna_main_input_metadata_t istd, inout pna_main_output_metadata_t ostd) {
    bit<32> tmpDir;
    ethernet_t eth;
    action next_hop(PortId_t vport) {
        eth = hdr.ethernet;
        hdr.ethernet1 = eth;
        send_to_port(vport);
    }
    action default_route_drop() {
        drop_packet();
        copy_header_ethernet(hdr.ethernet, hdr.ethernet1);
    }
    table ipv4_da_lpm {
        key = {
            tmpDir: lpm @name("ipv4_addr") ;
        }
        actions = {
            next_hop;
            default_route_drop;
        }
        const default_action = default_route_drop;
    }
    apply {
        if (PNA_Direction_t.NET_TO_HOST == istd.direction) {
            tmpDir = hdr.ipv4.srcAddr;
        } else {
            tmpDir = hdr.ipv4.dstAddr;
        }
        if (hdr.ipv4.isValid()) {
            ipv4_da_lpm.apply();
        }
    }
}

control MainDeparserImpl(packet_out pkt, in headers_t hdr, in main_metadata_t user_meta, in pna_main_output_metadata_t ostd) {
    apply {
        pkt.emit(hdr.ethernet);
        pkt.emit(hdr.ipv4);
    }
}

PNA_NIC(MainParserImpl(), PreControlImpl(), MainControlImpl(), MainDeparserImpl()) main;

