use gba_recomp::{arm, thumb, Flow};

#[test]
fn arm_branches_and_returns() {
    // b 0x080000C0 at 0x08000000
    let i = arm::decode(0xEA00_002E, 0x0800_0000);
    assert_eq!(i.flow, Flow::Branch(0x0800_00C0));
    // bl with the offset going backwards
    let i = arm::decode(0xEBFF_FFFE, 0x0800_0100);
    assert_eq!(i.flow, Flow::Call(0x0800_0100));
    // bx lr is a return, bx r1 is not
    assert_eq!(arm::decode(0xE12F_FF1E, 0).flow, Flow::Indirect { ret: true, thumb_switch: true });
    assert_eq!(arm::decode(0xE12F_FF11, 0).flow, Flow::Indirect { ret: false, thumb_switch: true });
    // conditional branch
    assert_eq!(arm::decode(0x0A00_0000, 0x0800_0000).flow, Flow::CondBranch(0x0800_0008));
    // mov pc, lr and ldmfd sp!, {pc}
    assert_eq!(arm::decode(0xE1A0_F00E, 0).flow, Flow::Indirect { ret: true, thumb_switch: false });
    assert_eq!(arm::decode(0xE8BD_8000, 0).flow, Flow::Indirect { ret: true, thumb_switch: false });
}

#[test]
fn arm_text() {
    assert_eq!(arm::decode(0xE3A0_0012, 0).text, "mov r0, #0x12");
    assert_eq!(arm::decode(0xE129_F000, 0).text, "msr cpsr_fc, r0");
    assert_eq!(arm::decode(0xE59F_D024, 0).text, "ldr sp, [pc, #0x24]");
    assert_eq!(arm::decode(0xE5810000, 0).text, "str r0, [r1]");
    assert_eq!(arm::decode(0xE0010392, 0).text, "mul r1, r2, r3");
    assert_eq!(arm::decode(0xEF00_0006, 0).flow, Flow::Swi(0));
    assert_eq!(arm::decode(0xEF06_0000, 0).flow, Flow::Swi(6));
}

#[test]
fn thumb_flow() {
    // bl: F7FF FF8B at 0x080B0522 -> 0x080B043C
    let i = thumb::decode(0xF7FF, 0xFF8B, 0x080B_0522);
    assert_eq!((i.size, i.flow), (4, Flow::Call(0x080B_043C)));
    // bx r0 / bx lr
    assert_eq!(thumb::decode(0x4700, 0, 0).flow, Flow::Indirect { ret: false, thumb_switch: true });
    assert_eq!(thumb::decode(0x4770, 0, 0).flow, Flow::Indirect { ret: true, thumb_switch: true });
    // push {r4, lr} / pop {r4, pc}
    assert_eq!(thumb::decode(0xB510, 0, 0).text, "push {r4, lr}");
    assert_eq!(thumb::decode(0xBD10, 0, 0).flow, Flow::Indirect { ret: true, thumb_switch: false });
    // beq +4, b -2 (an infinite loop)
    assert_eq!(thumb::decode(0xD002, 0, 0x0800_0100).flow, Flow::CondBranch(0x0800_0108));
    assert_eq!(thumb::decode(0xE7FE, 0, 0x0800_0100).flow, Flow::Branch(0x0800_0100));
    // swi 6
    assert_eq!(thumb::decode(0xDF06, 0, 0).flow, Flow::Swi(6));
}

#[test]
fn thumb_text() {
    assert_eq!(thumb::decode(0x2000, 0, 0).text, "movs r0, #0");
    assert_eq!(thumb::decode(0x1C40, 0, 0).text, "adds r0, r0, #1");
    assert_eq!(thumb::decode(0x4008, 0, 0).text, "ands r0, r1");
    assert_eq!(thumb::decode(0x2800, 0, 0).text, "cmp r0, #0");
    assert_eq!(thumb::decode(0x6020, 0, 0).text, "str r0, [r4, #0x0]");
}
