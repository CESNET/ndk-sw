from .pypcie.device import Device


FEATURE_FME_GUID = [0xf9e17764, 0x82fe38f0, 0x4a5246e3, 0xbfaf2ae9]

FEATURE_ID_AFU = 0xff

DFH_TYPE_AFU = 1
DFH_TYPE_PRIVATE = 3
DFH_TYPE_FIU = 4

DFH_TYPE_FIU_FME = 0
DFH_TYPE_FIU_PORT = 1

FEATURE_ID_FIU_HEADER = 0xfe
FEATURE_ID_AFU = 0xff

FME_HDR_CAP = 0x30

NEXT_AFU = 0x18


def bit(b):
    return (1 << b)


def bitmask(b):
    return (1 << b) - 1


def bit_get(val, b):
    return (val >> b) & 1


def field_get(val, bto, bfrom):
    return (val >> bfrom) & bitmask(bto - bfrom)


def feature_id(value):
    id = field_get(12, 0, value)
    type = field_get(64, 60, value)

    if type == DFH_TYPE_FIU:
        return FEATURE_ID_FIU_HEADER
    elif type == DFH_TYPE_PRIVATE:
        return id
    elif type == DFH_TYPE_AFU:
        return FEATURE_ID_AFU


def parse_dfl_from(bar, offset, out_dict, verbose):
    # read BAR 0, offset 0x1004
    while offset is not None:
        guid = []
        cfg_qword = bar.read64(offset+0)

        dfh_id = field_get(cfg_qword, 12, 0)
        dfh_type = field_get(cfg_qword, 64, 60)
        dfh_ver = field_get(cfg_qword, 60, 52)
        dfh_next = field_get(cfg_qword, 40, 16)
        eol = bit_get(cfg_qword, 40)

        dfh_types = {DFH_TYPE_AFU: "AFU", DFH_TYPE_FIU: "FIU", DFH_TYPE_PRIVATE: "PRIV"}
        dfh_type_s = dfh_types[dfh_type] if dfh_type in dfh_types else "Unknown"
        for i in range(4):
            guid.append(bar.read32(offset + 8 + i*4))

        if verbose:
            print(f"DFL: ITEM on offset {hex(offset)}: {dfh_type_s}, ID: {dfh_id}, version {dfh_ver}, next: {hex(dfh_next)}, eol: {eol}", "GUID:", [hex(x) for x in guid])
        if dfh_type == DFH_TYPE_FIU:
            #fme_next_afuq = bar.read64(offset + NEXT_AFU)
            #fme_next_afu = field_get(fme_next_afuq, 24, 0)

            if dfh_id == DFH_TYPE_FIU_FME:
                fme_cfg_qword = bar.read64(offset + FME_HDR_CAP)
                fme_ports = field_get(fme_cfg_qword, 20, 17)
                for p in range(fme_ports):
                    fme_port_cfg_qword = bar.read64(offset + FME_HDR_CAP + 8*p)
                    port_impl = bit_get(fme_port_cfg_qword, 60)
                    port_dfh_offset = field_get(fme_port_cfg_qword, 24, 0)
                    port_dfh_bar = field_get(fme_port_cfg_qword, 35, 32)

                    if verbose:
                        print("DFL: FME port ", port_impl, hex(fme_port_cfg_qword), hex(port_dfh_offset), hex(port_dfh_bar))

                    if not port_impl:
                        continue
                    parse_dfl_from(bar, port_dfh_offset, out_dict, verbose)

            elif dfh_id == DFH_TYPE_FIU_PORT:
                if verbose:
                    print("DFL: FIU port")
            else:
                print(f"DFL: unknown FIU {dfh_id}")
        elif dfh_type == DFH_TYPE_PRIVATE:
            out_dict[dfh_id] = (offset, dfh_next)
        else:
            if verbose:
                print(f"DFL: ITEM {dfh_type} {dfh_id}")

        if eol or dfh_next == 0:
            break
        offset += dfh_next


def parse_dfl(device, verbose):
    """
    Return features found in device as dict of {feature_number: (base_address, size)}.
    For feature list see:
    https://github.com/OFS/dfl-feature-id/blob/main/dfl-feature-ids.rst
    """

    dev = Device(device)
    bar = dev.bar[0]
    features = {}
    parse_dfl_from(bar, 0, features, verbose)
    return features
