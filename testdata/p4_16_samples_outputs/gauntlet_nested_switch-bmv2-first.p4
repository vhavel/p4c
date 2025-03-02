#include <core.p4>
#define V1MODEL_VERSION 20180101
#include <v1model.p4>

header ethernet_t {
    bit<48> dst_addr;
    bit<48> src_addr;
    bit<16> eth_type;
}

struct Headers {
    ethernet_t eth_hdr;
}

struct Meta {
}

parser p(packet_in pkt, out Headers hdr, inout Meta m, inout standard_metadata_t sm) {
    state start {
        transition parse_hdrs;
    }
    state parse_hdrs {
        pkt.extract<ethernet_t>(hdr.eth_hdr);
        transition accept;
    }
}

control ingress(inout Headers h, inout Meta m, inout standard_metadata_t sm) {
    action call_action(inout bit<48> val) {
    }
    table simple_table {
        actions = {
            @defaultonly NoAction();
        }
        default_action = NoAction();
    }
    apply {
        bit<16> simple_val = 16w2;
        call_action(h.eth_hdr.src_addr);
        if (simple_val <= 16w5) {
            return;
        } else {
            switch (simple_table.apply().action_run) {
                default: {
                    h.eth_hdr.dst_addr = 48w32;
                }
            }
        }
        h.eth_hdr.eth_type = 16w1;
    }
}

control vrfy(inout Headers h, inout Meta m) {
    apply {
    }
}

control update(inout Headers h, inout Meta m) {
    apply {
    }
}

control egress(inout Headers h, inout Meta m, inout standard_metadata_t sm) {
    apply {
    }
}

control deparser(packet_out b, in Headers h) {
    apply {
        b.emit<Headers>(h);
    }
}

V1Switch<Headers, Meta>(p(), vrfy(), ingress(), egress(), update(), deparser()) main;

