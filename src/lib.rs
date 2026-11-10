//! gba-recomp: static recompiler for the Game Boy Advance (ARM7TDMI, ARM and Thumb).

pub mod arm;
pub mod discover;
pub mod gen;
pub mod map;
pub mod rom;
pub mod thumb;

/// How an instruction continues the flow of control. Shared by the ARM and Thumb decoders.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum Flow {
    /// Falls through to the next instruction.
    Next,
    /// Conditional branch: continues at the next instruction and at `target`.
    CondBranch(u32),
    /// Unconditional branch to `target`; does not continue.
    Branch(u32),
    /// Call (`BL`) to `target`; continues at the next instruction when it returns.
    Call(u32),
    /// Conditional call: continues at the next instruction either way.
    CondCall(u32),
    /// `BX Rm` / write to PC that we cannot follow statically. `ret` when it looks like a return (`BX LR`, `POP {PC}`, `MOV PC, LR`).
    Indirect { ret: bool, thumb_switch: bool },
    /// Like `Indirect` but conditional: continues at the next instruction too.
    CondIndirect { ret: bool, thumb_switch: bool },
    /// `SWI`: continues at the next instruction (the BIOS call returns).
    Swi(u32),
    /// An encoding that is undefined on the ARM7TDMI.
    Undefined,
}

/// A decoded instruction: its text and its flow.
#[derive(Debug, Clone)]
pub struct Insn {
    pub size: u32,
    pub text: String,
    pub flow: Flow,
}
