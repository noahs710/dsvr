import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.Instruction;
import ghidra.program.model.listing.InstructionIterator;
import ghidra.program.model.symbol.Reference;
import ghidra.program.model.symbol.ReferenceIterator;

public class DescribeFunction extends GhidraScript {
    @Override
    public void run() throws Exception {
        for (String text : getScriptArgs()) {
            Address address = currentProgram.getAddressFactory().getAddress(text);
            if (address == null) {
                printerr("Unknown address: " + text);
                continue;
            }
            Function function = currentProgram.getFunctionManager().getFunctionContaining(address);
            println("=== address " + address + " ===");
            if (function == null) {
                println("No containing function");
                continue;
            }
            println("function=" + function.getName() + " entry=" + function.getEntryPoint() +
                " body=" + function.getBody().getMinAddress() + ".." +
                function.getBody().getMaxAddress() + " bytes=" + function.getBody().getNumAddresses() +
                " thunk=" + function.isThunk() + " signature=" + function.getSignature());
            if (function.isThunk()) {
                Function target = function.getThunkedFunction(true);
                println("thunk target=" + (target == null ? "<none>" : target.getEntryPoint()));
            }
            InstructionIterator instructions = currentProgram.getListing().getInstructions(function.getBody(), true);
            while (instructions.hasNext() && !monitor.isCancelled()) {
                Instruction instruction = instructions.next();
                println(instruction.getAddress() + "  " + instruction);
            }
            ReferenceIterator refs = currentProgram.getReferenceManager().getReferencesTo(function.getEntryPoint());
            while (refs.hasNext()) {
                Reference reference = refs.next();
                println("xref " + reference.getReferenceType() + " @ " + reference.getFromAddress());
            }
        }
    }
}
