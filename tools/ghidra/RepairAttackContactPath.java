import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.data.BooleanDataType;
import ghidra.program.model.data.ByteDataType;
import ghidra.program.model.data.DWordDataType;
import ghidra.program.model.data.FloatDataType;
import ghidra.program.model.data.PointerDataType;
import ghidra.program.model.data.VoidDataType;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.ParameterImpl;
import ghidra.program.model.symbol.SourceType;
import java.util.Arrays;

public class RepairAttackContactPath extends GhidraScript {
    private PointerDataType pointerTo(ghidra.program.model.data.DataType type) {
        return new PointerDataType(type, currentProgram.getDefaultPointerSize());
    }

    private Function functionAt(String text) {
        Address address = currentProgram.getAddressFactory().getAddress(text);
        Function function = address == null ? null : currentProgram.getFunctionManager().getFunctionAt(address);
        if (function == null) {
            throw new IllegalStateException("Expected function missing at " + text);
        }
        return function;
    }

    private void setSignature(Function function, ghidra.program.model.data.DataType result,
            ParameterImpl... parameters) throws Exception {
        function.setNoReturn(false);
        function.setReturnType(result, SourceType.USER_DEFINED);
        function.replaceParameters(Arrays.asList(parameters),
            Function.FunctionUpdateType.DYNAMIC_STORAGE_ALL_PARAMS, true, SourceType.USER_DEFINED);
        println(function.getEntryPoint() + " " + function.getSignature());
    }

    @Override
    public void run() throws Exception {
        PointerDataType voidPointer = pointerTo(VoidDataType.dataType);
        PointerDataType floatPointer = pointerTo(FloatDataType.dataType);
        PointerDataType bytePointer = pointerTo(ByteDataType.dataType);
        int transaction = currentProgram.startTransaction("Repair attack/contact ABI evidence");
        boolean committed = false;
        try {
            setSignature(functionAt("0x1405c93f0"), BooleanDataType.dataType,
                new ParameterImpl("arg1", voidPointer, currentProgram, SourceType.USER_DEFINED),
                new ParameterImpl("arg2", floatPointer, currentProgram, SourceType.USER_DEFINED),
                new ParameterImpl("arg3", DWordDataType.dataType, currentProgram, SourceType.USER_DEFINED),
                new ParameterImpl("arg4", DWordDataType.dataType, currentProgram, SourceType.USER_DEFINED),
                new ParameterImpl("arg5", FloatDataType.dataType, currentProgram, SourceType.USER_DEFINED));
            setSignature(functionAt("0x1405c9470"), BooleanDataType.dataType,
                new ParameterImpl("arg1", voidPointer, currentProgram, SourceType.USER_DEFINED),
                new ParameterImpl("arg2", floatPointer, currentProgram, SourceType.USER_DEFINED),
                new ParameterImpl("arg3", DWordDataType.dataType, currentProgram, SourceType.USER_DEFINED),
                new ParameterImpl("arg4", DWordDataType.dataType, currentProgram, SourceType.USER_DEFINED),
                new ParameterImpl("arg5", FloatDataType.dataType, currentProgram, SourceType.USER_DEFINED));
            setSignature(functionAt("0x1405c9930"), BooleanDataType.dataType,
                new ParameterImpl("arg1", voidPointer, currentProgram, SourceType.USER_DEFINED),
                new ParameterImpl("arg2", floatPointer, currentProgram, SourceType.USER_DEFINED),
                new ParameterImpl("arg3", DWordDataType.dataType, currentProgram, SourceType.USER_DEFINED),
                new ParameterImpl("arg4", DWordDataType.dataType, currentProgram, SourceType.USER_DEFINED),
                new ParameterImpl("arg5", FloatDataType.dataType, currentProgram, SourceType.USER_DEFINED));
            setSignature(functionAt("0x1405c7760"), VoidDataType.dataType,
                new ParameterImpl("arg1", voidPointer, currentProgram, SourceType.USER_DEFINED),
                new ParameterImpl("arg2", DWordDataType.dataType, currentProgram, SourceType.USER_DEFINED),
                new ParameterImpl("arg3", DWordDataType.dataType, currentProgram, SourceType.USER_DEFINED),
                new ParameterImpl("arg4", FloatDataType.dataType, currentProgram, SourceType.USER_DEFINED),
                new ParameterImpl("arg5", FloatDataType.dataType, currentProgram, SourceType.USER_DEFINED),
                new ParameterImpl("arg6", FloatDataType.dataType, currentProgram, SourceType.USER_DEFINED));
            setSignature(functionAt("0x1405debe0"), voidPointer,
                new ParameterImpl("outEvent", voidPointer, currentProgram, SourceType.USER_DEFINED),
                new ParameterImpl("arg2", voidPointer, currentProgram, SourceType.USER_DEFINED),
                new ParameterImpl("arg3", DWordDataType.dataType, currentProgram, SourceType.USER_DEFINED),
                new ParameterImpl("arg4", DWordDataType.dataType, currentProgram, SourceType.USER_DEFINED),
                new ParameterImpl("arg5", FloatDataType.dataType, currentProgram, SourceType.USER_DEFINED),
                new ParameterImpl("arg6", FloatDataType.dataType, currentProgram, SourceType.USER_DEFINED),
                new ParameterImpl("arg7", FloatDataType.dataType, currentProgram, SourceType.USER_DEFINED));
            setSignature(functionAt("0x1405eb350"), BooleanDataType.dataType,
                new ParameterImpl("arg1", voidPointer, currentProgram, SourceType.USER_DEFINED),
                new ParameterImpl("arg2", floatPointer, currentProgram, SourceType.USER_DEFINED),
                new ParameterImpl("arg3", floatPointer, currentProgram, SourceType.USER_DEFINED),
                new ParameterImpl("arg4", voidPointer, currentProgram, SourceType.USER_DEFINED));
            setSignature(functionAt("0x1405ea120"), BooleanDataType.dataType,
                new ParameterImpl("arg1", voidPointer, currentProgram, SourceType.USER_DEFINED),
                new ParameterImpl("arg2", floatPointer, currentProgram, SourceType.USER_DEFINED),
                new ParameterImpl("arg3", voidPointer, currentProgram, SourceType.USER_DEFINED),
                new ParameterImpl("arg4", floatPointer, currentProgram, SourceType.USER_DEFINED),
                new ParameterImpl("arg5", voidPointer, currentProgram, SourceType.USER_DEFINED),
                new ParameterImpl("arg6", bytePointer, currentProgram, SourceType.USER_DEFINED),
                new ParameterImpl("arg7", DWordDataType.dataType, currentProgram, SourceType.USER_DEFINED));
            ParameterImpl[] damageEventParams = new ParameterImpl[18];
            damageEventParams[0] = new ParameterImpl("arg1", voidPointer, currentProgram, SourceType.USER_DEFINED);
            damageEventParams[1] = new ParameterImpl("arg2", DWordDataType.dataType, currentProgram, SourceType.USER_DEFINED);
            for (int i = 2; i < damageEventParams.length; i++) {
                damageEventParams[i] = new ParameterImpl("arg" + (i + 1), FloatDataType.dataType,
                    currentProgram, SourceType.USER_DEFINED);
            }
            setSignature(functionAt("0x1405d1ba0"), BooleanDataType.dataType, damageEventParams);
            setSignature(functionAt("0x1405d28b0"), voidPointer,
                new ParameterImpl("arg1", voidPointer, currentProgram, SourceType.USER_DEFINED));
            setSignature(functionAt("0x1405d4ae0"), voidPointer,
                new ParameterImpl("arg1", voidPointer, currentProgram, SourceType.USER_DEFINED),
                new ParameterImpl("arg2", voidPointer, currentProgram, SourceType.USER_DEFINED));
            setSignature(functionAt("0x1405b3c10"), BooleanDataType.dataType,
                new ParameterImpl("arg1", voidPointer, currentProgram, SourceType.USER_DEFINED));
            setSignature(functionAt("0x1405c4ac0"), voidPointer,
                new ParameterImpl("arg1", voidPointer, currentProgram, SourceType.USER_DEFINED),
                new ParameterImpl("arg2", DWordDataType.dataType, currentProgram, SourceType.USER_DEFINED));
            setSignature(functionAt("0x1405d1470"), VoidDataType.dataType,
                new ParameterImpl("arg1", voidPointer, currentProgram, SourceType.USER_DEFINED),
                new ParameterImpl("arg2", DWordDataType.dataType, currentProgram, SourceType.USER_DEFINED));
            setSignature(functionAt("0x140629330"), voidPointer,
                new ParameterImpl("arg1", voidPointer, currentProgram, SourceType.USER_DEFINED),
                new ParameterImpl("arg2", DWordDataType.dataType, currentProgram, SourceType.USER_DEFINED));
            currentProgram.endTransaction(transaction, true);
            committed = true;
        } finally {
            if (!committed) {
                currentProgram.endTransaction(transaction, false);
            }
        }
    }
}
