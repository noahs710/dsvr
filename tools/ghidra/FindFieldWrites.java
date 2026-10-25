import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.FunctionIterator;
import ghidra.program.model.listing.Instruction;
import ghidra.program.model.listing.InstructionIterator;
import ghidra.program.model.symbol.RefType;
import ghidra.program.model.scalar.Scalar;
import ghidra.program.model.lang.Register;

// Find every instruction whose operand uses the register-indirect form
// [base(+index*scale) + displacement] and performs a data access on it.
// Usage: FindFieldWrites <displacement> [access] [funcStart funcEnd]
//   displacement : hex (0x..) or decimal signed displacement
//   access       : w = write only (default), r = read only, rw = any access
//   funcStart/End: optional inclusive function-entry address range
// Immediate-only and RIP-relative (no base register) operands are excluded:
// they name a global, not a field of an object.
public class FindFieldWrites extends GhidraScript {
    @Override
    public void run() throws Exception {
        String[] args = getScriptArgs();
        if (args.length < 1) {
            printerr("Usage: FindFieldWrites <displacement> [w|r|rw] [funcStart funcEnd]");
            return;
        }
        long disp = Long.decode(args[0]).longValue();
        char access = args.length >= 2 ? args[1].toLowerCase().charAt(0) : 'w';
        Address funcStart = null, funcEnd = null;
        if (args.length >= 4) {
            funcStart = currentProgram.getAddressFactory().getAddress(args[2]);
            funcEnd = currentProgram.getAddressFactory().getAddress(args[3]);
            if (funcStart == null || funcEnd == null) {
                printerr("Invalid function range");
                return;
            }
        }
        println("=== field-writes disp=" + Long.toHexString(disp)
            + " access=" + access
            + (funcStart == null ? " range=all" : " range=" + funcStart + ".." + funcEnd) + " ===");
        long hits = 0, funcs = 0;
        FunctionIterator functions = currentProgram.getFunctionManager().getFunctions(true);
        while (functions.hasNext() && !monitor.isCancelled()) {
            Function function = functions.next();
            Address entry = function.getEntryPoint();
            if (funcStart != null && (entry.compareTo(funcStart) < 0 || entry.compareTo(funcEnd) > 0))
                continue;
            funcs++;
            InstructionIterator instructions = currentProgram.getListing().getInstructions(function.getBody(), true);
            while (instructions.hasNext() && !monitor.isCancelled()) {
                Instruction instruction = instructions.next();
                for (int i = 0; i < instruction.getNumOperands(); i++) {
                    Object[] objects = instruction.getOpObjects(i);
                    // register-indirect form: at least one Register (base/index)
                    // and one Scalar displacement, base register first.
                    if (objects.length < 2 || !(objects[0] instanceof Register)) continue;
                    Scalar displacement = null;
                    boolean hasRegister = false;
                    for (Object object : objects) {
                        if (object instanceof Register) hasRegister = true;
                        else if (object instanceof Scalar) { displacement = (Scalar) object; break; }
                    }
                    if (!hasRegister || displacement == null) continue;
                    if (displacement.getValue() != disp) continue;
                    RefType refType = instruction.getOperandRefType(i);
                    boolean isWrite = refType != null && refType.isWrite();
                    boolean isRead = refType != null && refType.isRead();
                    boolean wanted;
                    switch (access) {
                        case 'r': wanted = isRead; break;
                        case 'w': wanted = isWrite; break;
                        default: wanted = isRead || isWrite; break;
                    }
                    if (!wanted) continue; // e.g. LEA operand access classification
                    hits++;
                    println(entry + " " + function.getName()
                        + " | " + instruction.getAddress() + " " + instruction
                        + " | op=" + i + " ref=" + refType);
                }
            }
        }
        println("=== scan complete: functions=" + funcs + " hits=" + hits + " ===");
    }
}
