import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileResults;
import ghidra.app.cmd.disassemble.DisassembleCommand;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.address.AddressSet;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.FlowOverride;
import ghidra.program.model.listing.Instruction;
import ghidra.program.model.symbol.FlowType;
import ghidra.program.model.symbol.SourceType;
import java.util.ArrayDeque;
import java.util.HashSet;
import java.util.Set;
import java.util.ArrayList;
import java.util.List;
import java.util.Map;
import java.util.regex.Matcher;
import java.util.regex.Pattern;
import java.util.LinkedHashMap;

// Analysis only: follow direct jump fragments across artificial function
// boundaries, print their instructions, then decompile the combined body.
// Calls are never traversed; unresolved branches and bounds are reported.
// Every database change is rolled back, including on failure.
public class RecoverSplitFunction extends GhidraScript {
    private Address scratchBase;
    private int scratchUsed;
    private final Map<Address,byte[]> originalBytes=new LinkedHashMap<>();

    // Read-only queries may need an analysis-only branch island when the
    // original PUSH/JMP fragment has room for JMP, but not Jcc+JMP. Never use
    // an executable code cave: a new transaction-owned block is rolled back.
    private Address branchIsland(Address fallback, Address alternate, int condition) throws Exception {
        if (scratchBase == null) {
            scratchBase=toAddr("144000000");
            if (currentProgram.getMemory().getBlock(scratchBase)!=null)
                throw new IllegalStateException("Analysis scratch address is occupied");
            currentProgram.getMemory().createInitializedBlock(
                "TemporaryReturnDispatch",scratchBase,0x10000,(byte)0,monitor,false).setExecute(true);
        }
        if (scratchUsed+16>0x10000) throw new IllegalStateException("Analysis branch island bound");
        Address at=scratchBase.add(scratchUsed); scratchUsed+=16;
        byte[] code=new byte[11];
        code[0]=0x0f;code[1]=(byte)condition;
        relative(code,2,alternate,at.add(6));
        code[6]=(byte)0xe9;relative(code,7,fallback,at.add(11));
        currentProgram.getMemory().setBytes(at,code);
        disassemble(at);
        return at;
    }

    // LEA RSP,[RSP+8]; JMP qword ptr [RSP-8] is exactly RET, including
    // flags preservation. This rule does not resolve arbitrary indirect JMPs.
    private boolean returnPair(Instruction instruction) throws Exception {
        if (!compact(instruction).equals("LEARSP,[RSP+0X8]")) return false;
        Instruction next=instructionAt(instruction.getMaxAddress().add(1));
        return next!=null && compact(next).equals("JMPQWORDPTR[RSP+-0X8]");
    }

    private boolean normalizeReturnPair(Address start) throws Exception {
        Instruction instruction=instructionAt(start);
        if (instruction==null || !returnPair(instruction)) return false;
        byte[] original=new byte[instruction.getLength()];
        currentProgram.getMemory().getBytes(start,original);
        originalBytes.putIfAbsent(start,original);
        for (int i=0;i<original.length;i++) pristineBytes.putIfAbsent(start.add(i),original[i]);
        byte[] replacement=new byte[original.length];
        java.util.Arrays.fill(replacement,(byte)0x90);replacement[0]=(byte)0xc3;
        clearListing(start,instruction.getMaxAddress());
        currentProgram.getMemory().setBytes(start,replacement);disassemble(start);
        println("ANALYSIS-ONLY normalized exact return pair "+start);
        return true;
    }
    private final Map<Address,Byte> pristineBytes=new LinkedHashMap<>();
    private Instruction instructionAt(Address address) throws Exception {
        Instruction instruction=currentProgram.getListing().getInstructionAt(address);
        if (instruction==null && currentProgram.getMemory().getBlock(address)!=null &&
            currentProgram.getMemory().getBlock(address).isExecute()) {
            new DisassembleCommand(address,new AddressSet(address,address.add(15)),false)
                .applyTo(currentProgram,monitor);
            instruction=currentProgram.getListing().getInstructionAt(address);
        }
        return instruction;
    }
    private String compact(Instruction instruction) {
        return instruction.toString().replace(" ", "").toUpperCase();
    }

    private void relative(byte[] bytes, int offset, Address target, Address next) {
        long displacement = target.subtract(next);
        if (displacement < Integer.MIN_VALUE || displacement > Integer.MAX_VALUE)
            throw new IllegalArgumentException("Branch out of range");
        for (int i=0;i<4;i++) bytes[offset+i]=(byte)(displacement >> (8*i));
    }

    // Recognize only the exact stack-neutral PUSH/MOV/XCHG[/CMOV]/RET
    // dispatch idiom. This replaces analysis-memory bytes with equivalent
    // jumps inside the transaction; the executable is never written.
    private boolean normalizeReturnDispatch(Address start) throws Exception {
        List<Instruction> operations = new ArrayList<>();
        Set<Address> path = new HashSet<>();
        Address cursor=start;
        for (int count=0;count<64;count++) {
            if (!path.add(cursor)) return false;
            Instruction instruction=instructionAt(cursor);
            if (instruction==null) return false;
            if (instruction.getMnemonicString().equals("JMP")) {
                instruction.setFlowOverride(FlowOverride.NONE);
                Address[] flows=instruction.getFlows();
                if (flows.length!=1) return false;
                cursor=flows[0];continue;
            }
            operations.add(instruction);
            if (instruction.getMnemonicString().equals("RET") || returnPair(instruction)) break;
            if (operations.size()>12) return false;
            cursor=instruction.getMaxAddress().add(1);
        }
        if (operations.size()!=4 && operations.size()!=12) return false;
        String save=compact(operations.get(0));
        if (!save.matches("PUSHR[A-Z0-9]+")) return false;
        String savedRegister=save.substring(4);
        if (savedRegister.equals("RSP")) return false;
        Matcher load=Pattern.compile("(?:MOV"+savedRegister+",(0X[0-9A-F]+)|LEA"+
            savedRegister+",\\[(0X[0-9A-F]+)\\])")
            .matcher(compact(operations.get(1)));
        if (!load.matches() || !compact(operations.get(2)).equals("XCHGQWORDPTR[RSP],"+savedRegister))
            return false;
        Address fallback=toAddr(load.group(1)!=null?load.group(1):load.group(2));
        Address alternate=null;
        int condition=0;
        if (operations.size()==12) {
            String pushA=compact(operations.get(3)),pushB=compact(operations.get(4));
            if (!pushA.matches("PUSHR[A-Z0-9]+") || !pushB.matches("PUSHR[A-Z0-9]+")) return false;
            String regA=pushA.substring(4),regB=pushB.substring(4);
            if (regA.equals(regB) || regA.equals("RSP") || regB.equals("RSP")) return false;
            if (!compact(operations.get(5)).equals("MOV"+regA+",QWORDPTR[RSP+0X10]")) return false;
            Matcher alt=Pattern.compile("MOV"+regB+",(0X[0-9A-F]+)")
                .matcher(compact(operations.get(6)));
            if (!alt.matches()) return false;
            alternate=toAddr(alt.group(1));
            Map<String,Integer> codes=Map.ofEntries(
                Map.entry("Z",0x84),Map.entry("NZ",0x85),Map.entry("B",0x82),Map.entry("AE",0x83),
                Map.entry("BE",0x86),Map.entry("A",0x87),Map.entry("L",0x8c),Map.entry("GE",0x8d),
                Map.entry("LE",0x8e),Map.entry("G",0x8f),Map.entry("S",0x88),Map.entry("NS",0x89),
                Map.entry("O",0x80),Map.entry("NO",0x81),Map.entry("P",0x8a),Map.entry("NP",0x8b),
                Map.entry("NC",0x83),Map.entry("C",0x82),Map.entry("NBE",0x87),Map.entry("NA",0x86),
                Map.entry("NL",0x8d),Map.entry("NGE",0x8c),Map.entry("NLE",0x8f),Map.entry("NG",0x8e));
            String mnemonic=operations.get(7).getMnemonicString();
            Integer opcode=mnemonic.startsWith("CMOV")?codes.get(mnemonic.substring(4)):null;
            if (opcode==null || !compact(operations.get(7)).equals(mnemonic+regA+","+regB) ||
                !compact(operations.get(8)).equals("MOVQWORDPTR[RSP+0X10],"+regA) ||
                !compact(operations.get(9)).equals("POP"+regB) ||
                !compact(operations.get(10)).equals("POP"+regA)) return false;
            condition=opcode;
        }
        Instruction terminal=operations.get(operations.size()-1);
        if (!compact(terminal).equals("RET") && !returnPair(terminal)) return false;
        if (instructionAt(fallback)==null ||
            (alternate!=null && instructionAt(alternate)==null)) return false;
        int required=alternate==null?5:11;
        Address end=start;
        while (end.subtract(start)<required) {
            if (!path.contains(end)) break;
            Instruction instruction=currentProgram.getListing().getInstructionAt(end);
            if (instruction==null) break;
            end=instruction.getMaxAddress().add(1);
        }
        // Cross-fragment return trampolines often expose only PUSH+JMP (6
        // bytes). A five-byte jump into an analysis-only island preserves the
        // original condition without overwriting unrelated instructions.
        boolean island=end.subtract(start)<required;
        if (end.subtract(start)<5) return false;
        Address dispatch=island?branchIsland(fallback,alternate,condition):fallback;
        byte[] replacement=new byte[(int)end.subtract(start)];
        byte[] original=new byte[replacement.length];
        currentProgram.getMemory().getBytes(start,original);
        originalBytes.putIfAbsent(start,original);
        for (int i=0;i<original.length;i++) pristineBytes.putIfAbsent(start.add(i),original[i]);
        java.util.Arrays.fill(replacement,(byte)0x90);
        int jump=0;
        if (alternate!=null && !island) {
            replacement[0]=0x0f;replacement[1]=(byte)condition;
            relative(replacement,2,alternate,start.add(6));jump=6;
        }
        replacement[jump]=(byte)0xe9;
        relative(replacement,jump+1,dispatch,start.add(jump+5));
        clearListing(start,end.subtract(1));
        currentProgram.getMemory().setBytes(start,replacement);
        disassemble(start);
        println("ANALYSIS-ONLY normalized return dispatch "+start+" default="+fallback+
            " alternate="+alternate+" condition="+condition);
        return true;
    }

    @Override
    public void run() throws Exception {
        for (String argument : getScriptArgs()) {
            Address entry = currentProgram.getAddressFactory().getAddress(argument);
            Function function = entry == null ? null :
                currentProgram.getFunctionManager().getFunctionAt(entry);
            if (entry == null || currentProgram.getMemory().getBlock(entry)==null ||
                !currentProgram.getMemory().getBlock(entry).isExecute())
                throw new IllegalArgumentException("Invalid executable entry: " + argument);
            int transaction = currentProgram.startTransaction("Temporary split-body recovery");
            originalBytes.clear();
            pristineBytes.clear();
            scratchBase=null;scratchUsed=0;
            DecompInterface decompiler = new DecompInterface();
            try {
                ArrayDeque<Address> pending = new ArrayDeque<>();
                Set<Address> visited = new HashSet<>();
                Set<Address> owners = new HashSet<>();
                AddressSet body = new AddressSet();
                pending.add(entry);
                int unresolved = 0, returns = 0;
                println("=== split body " + entry + " ===");
                while (!pending.isEmpty() && visited.size() < 4096 && !monitor.isCancelled()) {
                    Address address = pending.removeFirst();
                    if (!visited.add(address)) continue;
                    Instruction instruction = instructionAt(address);
                    if (instruction == null) {
                        println("UNRESOLVED instruction " + address);
                        unresolved++;
                        continue;
                    }
                    if ((compact(instruction).matches("PUSHR[A-Z0-9]+") && normalizeReturnDispatch(address)) ||
                        normalizeReturnPair(address))
                        instruction=currentProgram.getListing().getInstructionAt(address);
                    body.addRange(instruction.getMinAddress(), instruction.getMaxAddress());
                    Function owner = currentProgram.getFunctionManager().getFunctionContaining(address);
                    if (owner != null && !owner.getEntryPoint().equals(entry))
                        owners.add(owner.getEntryPoint());
                    println(address + "  " + instruction);
                    // The database may have converted a jump fragment into
                    // CALL_RETURN, which would follow dead bytes after it.
                    instruction.setFlowOverride(FlowOverride.NONE);
                    FlowType flow = instruction.getFlowType();
                    if (flow.isCall()) {
                        // Ignore erroneous no-return metadata at a call boundary.
                        // The following instruction must still exist in Listing.
                        pending.add(instruction.getMaxAddress().add(1));
                    } else {
                        if (instruction.getFallThrough() != null)
                            pending.add(instruction.getFallThrough());
                        if (flow.isJump()) {
                            Address[] destinations = instruction.getFlows();
                            for (Address destination : destinations) pending.add(destination);
                            if (destinations.length == 0) {
                                println("UNRESOLVED indirect branch " + address);
                                unresolved++;
                            }
                        }
                        if (flow.isTerminal()) {
                            // RET may implement a push/xchg return trampoline,
                            // not a caller return. Coverage cannot certify it.
                            returns++;
                            println("REVIEW terminal/return target " + address);
                        }
                    }
                }
                println("COVERAGE instructions=" + visited.size() + " unresolved=" + unresolved +
                    " terminal_targets_to_review=" + returns +
                    " pending=" + pending.size() + " cancelled=" + monitor.isCancelled());
                if (function!=null && function.isThunk()) function.setThunkedFunction(null);
                for (Address owner : owners)
                    currentProgram.getFunctionManager().removeFunction(owner);
                currentProgram.getFunctionManager().removeFunction(entry);
                function = currentProgram.getFunctionManager().createFunction(
                    "RecoveredSplit", entry, body, SourceType.USER_DEFINED);
                function.setNoReturn(false);
                decompiler.openProgram(currentProgram);
                DecompileResults result = decompiler.decompileFunction(function, 60, monitor);
                if (result.decompileCompleted() && result.getDecompiledFunction() != null)
                    println(result.getDecompiledFunction().getC());
                else println("DECOMPILATION INCOMPLETE " + result.getErrorMessage());
            } finally {
                decompiler.dispose();
                // Memory block buffers may outlive a rolled-back metadata
                // transaction. Restore every original byte explicitly too.
                try {
                    // Clear patched instructions before restoring: writing
                    // through live instruction objects caused MemoryAccessException.
                    for (Map.Entry<Address,byte[]> saved:originalBytes.entrySet())
                        clearListing(saved.getKey(),saved.getKey().add(saved.getValue().length-1));
                    for (Map.Entry<Address,Byte> saved:pristineBytes.entrySet())
                        currentProgram.getMemory().setByte(saved.getKey(),saved.getValue());
                    if (scratchBase!=null) {
                        var block=currentProgram.getMemory().getBlock(scratchBase);
                        if (block!=null) {
                            clearListing(block.getStart(),block.getEnd());
                            currentProgram.getMemory().removeBlock(block,monitor);
                        }
                    }
                } finally {
                    currentProgram.endTransaction(transaction, false);
                }
                for (Map.Entry<Address,Byte> saved:pristineBytes.entrySet()) {
                    if (currentProgram.getMemory().getByte(saved.getKey())!=saved.getValue())
                        throw new IllegalStateException("Analysis rollback mismatch: "+saved.getKey());
                }
                if (scratchBase!=null && currentProgram.getMemory().getBlock(scratchBase)!=null)
                    throw new IllegalStateException("Analysis scratch block survived rollback");
                println("Temporary analysis rolled back; verified "+originalBytes.size()+" byte ranges; scratch removed.");
            }
        }
    }
}
