#include <core.p4>
#include <bmv2/psa.p4>

header EMPTY_H {
}

struct EMPTY_RESUB {
}

struct EMPTY_CLONE {
}

struct EMPTY_BRIDGE {
}

struct EMPTY_RECIRC {
}

header ethernet_t {
    bit<48> dstAddr;
    bit<48> srcAddr;
    bit<16> etherType;
}

struct metadata {
    bit<32> meta;
    bit<32> meta1;
    bit<16> meta2;
    bit<32> meta3;
    bit<32> meta4;
    bit<16> meta5;
    bit<32> meta6;
    bit<16> meta7;
}

parser MyIP(packet_in buffer, out ethernet_t h, inout metadata b, in psa_ingress_parser_input_metadata_t c, in EMPTY_RESUB d, in EMPTY_RECIRC e) {
    state start {
        buffer.extract<ethernet_t>(h);
        transition accept;
    }
}

parser MyEP(packet_in buffer, out EMPTY_H a, inout metadata b, in psa_egress_parser_input_metadata_t c, in EMPTY_BRIDGE d, in EMPTY_CLONE e, in EMPTY_CLONE f) {
    state start {
        transition accept;
    }
}

control MyIC(inout ethernet_t a, inout metadata b, in psa_ingress_input_metadata_t c, inout psa_ingress_output_metadata_t d) {
    @noWarn("unused") @name(".NoAction") action NoAction_1() {
    }
    @name("MyIC.forward") action forward() {
    }
    @name("MyIC.tbl") table tbl_0 {
        key = {
            a.srcAddr: exact @name("a.srcAddr") ;
        }
        actions = {
            NoAction_1();
            forward();
        }
        default_action = NoAction_1();
    }
    @hidden action psadpdkbinaryoperations79() {
        b.meta = 32w1 << b.meta2;
        b.meta1 = 32w0x800 >> b.meta2;
        b.meta2 = 16w0xf0 - b.meta2;
        b.meta4 = 32w0x808 + b.meta6;
        b.meta6 = 32w0x808 - b.meta3;
        b.meta3 = b.meta3 + 32w0x1;
        b.meta5 = b.meta7 + 16w0xf0;
        b.meta7 = 16w0xf0 + b.meta2;
        a.dstAddr = (bit<48>)b.meta;
        a.srcAddr = (bit<48>)b.meta1;
        a.etherType = b.meta2;
    }
    @hidden table tbl_psadpdkbinaryoperations79 {
        actions = {
            psadpdkbinaryoperations79();
        }
        const default_action = psadpdkbinaryoperations79();
    }
    apply {
        tbl_0.apply();
        tbl_psadpdkbinaryoperations79.apply();
    }
}

control MyEC(inout EMPTY_H a, inout metadata b, in psa_egress_input_metadata_t c, inout psa_egress_output_metadata_t d) {
    apply {
    }
}

control MyID(packet_out buffer, out EMPTY_CLONE a, out EMPTY_RESUB b, out EMPTY_BRIDGE c, inout ethernet_t d, in metadata e, in psa_ingress_output_metadata_t f) {
    apply {
    }
}

control MyED(packet_out buffer, out EMPTY_CLONE a, out EMPTY_RECIRC b, inout EMPTY_H c, in metadata d, in psa_egress_output_metadata_t e, in psa_egress_deparser_input_metadata_t f) {
    apply {
    }
}

IngressPipeline<ethernet_t, metadata, EMPTY_BRIDGE, EMPTY_CLONE, EMPTY_RESUB, EMPTY_RECIRC>(MyIP(), MyIC(), MyID()) ip;

EgressPipeline<EMPTY_H, metadata, EMPTY_BRIDGE, EMPTY_CLONE, EMPTY_CLONE, EMPTY_RECIRC>(MyEP(), MyEC(), MyED()) ep;

PSA_Switch<ethernet_t, metadata, EMPTY_H, metadata, EMPTY_BRIDGE, EMPTY_CLONE, EMPTY_CLONE, EMPTY_RESUB, EMPTY_RECIRC>(ip, PacketReplicationEngine(), ep, BufferingQueueingEngine()) main;

