import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import ghidra.program.model.symbol.Reference;
import ghidra.program.model.symbol.ReferenceIterator;

public class ListDirectCallers extends GhidraScript {
    @Override
    public void run() throws Exception {
        for (String text : getScriptArgs()) {
            Address target = currentProgram.getAddressFactory().getAddress(text);
            if (target == null) {
                printerr("Unknown address: " + text);
                continue;
            }
            println("=== direct callers of " + target + " ===");
            ReferenceIterator references = currentProgram.getReferenceManager().getReferencesTo(target);
            int count = 0;
            while (references.hasNext() && count < 16 && !monitor.isCancelled()) {
                Reference reference = references.next();
                if (!reference.getReferenceType().isCall()) {
                    continue;
                }
                Function caller = currentProgram.getFunctionManager()
                    .getFunctionContaining(reference.getFromAddress());
                println((caller == null ? "<no function>" : caller.getName()) +
                    " @ " + reference.getFromAddress());
                ++count;
            }
            println("direct call sites shown: " + count);
        }
    }
}
