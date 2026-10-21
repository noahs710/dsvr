import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileResults;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.address.AddressSet;
import ghidra.program.model.data.BooleanDataType;
import ghidra.program.model.data.ByteDataType;
import ghidra.program.model.data.DWordDataType;
import ghidra.program.model.data.PointerDataType;
import ghidra.program.model.data.QWordDataType;
import ghidra.program.model.data.VoidDataType;
import ghidra.program.model.listing.FlowOverride;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.Instruction;
import ghidra.program.model.listing.ParameterImpl;
import ghidra.program.model.symbol.SourceType;
import java.util.Arrays;

public class RepairAttackEventFactory extends GhidraScript {
    @Override
    public void run() throws Exception {
        Address allocatorAddress = currentProgram.getAddressFactory().getAddress("0x140cc3e10");
        Address factoryStart = currentProgram.getAddressFactory().getAddress("0x1404e8350");
        Address factoryEnd = currentProgram.getAddressFactory().getAddress("0x1404e8464");
        Function allocator = currentProgram.getFunctionManager().getFunctionAt(allocatorAddress);
        Function factory = currentProgram.getFunctionManager().getFunctionAt(factoryStart);
        if (allocator == null || factory == null) {
            throw new IllegalStateException("Expected allocator or event-factory function missing");
        }

        int transaction = currentProgram.startTransaction("Repair event factory analysis");
        boolean committed = false;
        try {
            allocator.setNoReturn(false);
            allocator.setReturnType(new PointerDataType(VoidDataType.dataType, 8), SourceType.USER_DEFINED);
            allocator.replaceParameters(Arrays.asList(
                new ParameterImpl("size", QWordDataType.dataType, currentProgram, SourceType.USER_DEFINED),
                new ParameterImpl("alignment", QWordDataType.dataType, currentProgram, SourceType.USER_DEFINED),
                new ParameterImpl("heap", new PointerDataType(VoidDataType.dataType, 8), currentProgram,
                    SourceType.USER_DEFINED)),
                Function.FunctionUpdateType.DYNAMIC_STORAGE_ALL_PARAMS, true, SourceType.USER_DEFINED);
            factory.setBody(new AddressSet(factoryStart, factoryEnd));
            factory.setNoReturn(false);
            factory.setReturnType(BooleanDataType.dataType, SourceType.USER_DEFINED);
            factory.replaceParameters(Arrays.asList(
                new ParameterImpl("queue", new PointerDataType(VoidDataType.dataType, 8), currentProgram,
                    SourceType.USER_DEFINED),
                new ParameterImpl("eventType", DWordDataType.dataType, currentProgram, SourceType.USER_DEFINED),
                new ParameterImpl("payload", new PointerDataType(VoidDataType.dataType, 8), currentProgram,
                    SourceType.USER_DEFINED),
                new ParameterImpl("arg4", ByteDataType.dataType, currentProgram, SourceType.USER_DEFINED),
                new ParameterImpl("arg5", DWordDataType.dataType, currentProgram, SourceType.USER_DEFINED),
                new ParameterImpl("arg6", DWordDataType.dataType, currentProgram, SourceType.USER_DEFINED),
                new ParameterImpl("arg7", DWordDataType.dataType, currentProgram, SourceType.USER_DEFINED),
                new ParameterImpl("arg8", ByteDataType.dataType, currentProgram, SourceType.USER_DEFINED),
                new ParameterImpl("arg9", ByteDataType.dataType, currentProgram, SourceType.USER_DEFINED)),
                Function.FunctionUpdateType.DYNAMIC_STORAGE_ALL_PARAMS, true, SourceType.USER_DEFINED);
            Address allocatorCallAddress = currentProgram.getAddressFactory().getAddress("0x1404e838b");
            Instruction allocatorCall = currentProgram.getListing().getInstructionAt(allocatorCallAddress);
            if (allocatorCall == null) {
                throw new IllegalStateException("Allocator call instruction missing");
            }
            println("Allocator call flow override before repair: " + allocatorCall.getFlowOverride());
            allocatorCall.setFlowOverride(FlowOverride.NONE);
            currentProgram.endTransaction(transaction, true);
            committed = true;
        } finally {
            if (!committed) {
                currentProgram.endTransaction(transaction, false);
            }
        }

        DecompInterface decompiler = new DecompInterface();
        decompiler.openProgram(currentProgram);
        try {
            for (Function function : new Function[] { allocator, factory }) {
                println("=== " + function.getName() + " @ " + function.getEntryPoint()
                    + " noReturn=" + function.hasNoReturn() + " body=" + function.getBody()
                    + " ===");
                DecompileResults result = decompiler.decompileFunction(function, 60, monitor);
                if (!result.decompileCompleted() || result.getDecompiledFunction() == null) {
                    printerr("Could not decompile " + function.getEntryPoint());
                    continue;
                }
                println(result.getDecompiledFunction().getC());
            }
        } finally {
            decompiler.dispose();
        }
    }
}
