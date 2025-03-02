#include <core.p4>
#define V1MODEL_VERSION 20180101
#include <v1model.p4>

struct Headers {
}

struct Meta {
    @field_list(8w1) 
    bit<1> b;
}

parser p(packet_in b, out Headers h, inout Meta m, inout standard_metadata_t sm) {
    state start {
        m.b = m.b + 1w1;
        transition accept;
    }
}

control ingress(inout Headers h, inout Meta m, inout standard_metadata_t sm) {
    @hidden action issue10011bmv2l24() {
        clone_preserving_field_list(CloneType.I2E, 32w64, 8w1);
    }
    @hidden table tbl_issue10011bmv2l24 {
        actions = {
            issue10011bmv2l24();
        }
        const default_action = issue10011bmv2l24();
    }
    apply {
        tbl_issue10011bmv2l24.apply();
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
    }
}

V1Switch<Headers, Meta>(p(), vrfy(), ingress(), egress(), update(), deparser()) main;

