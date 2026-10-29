// Focused, read-only export from the already imported QuizWorld102.elf.
// @category Buzz
import java.io.BufferedWriter;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.Paths;
import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileResults;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.listing.Function;
import ghidra.program.model.symbol.ReferenceIterator;
import ghidra.program.model.symbol.Reference;

public class ExportBuzzVideo extends GhidraScript {
    @Override protected void run() throws Exception {
        String[] args=getScriptArgs();
        if(args.length<2)throw new IllegalArgumentException("output and hex function addresses required");
        Path path=Paths.get(args[0]); Files.createDirectories(path.getParent());
        DecompInterface decompiler=new DecompInterface();
        decompiler.toggleCCode(true); decompiler.toggleSyntaxTree(true);
        if(!decompiler.openProgram(currentProgram))throw new IllegalStateException("openProgram failed");
        try(BufferedWriter out=Files.newBufferedWriter(path,StandardCharsets.UTF_8)) {
            out.write("/* Program: "+currentProgram.getName()+" */\n");
            for(int i=1;i<args.length;++i) {
                Function function=currentProgram.getFunctionManager().getFunctionAt(toAddr(args[i]));
                out.write("\n/* Requested "+args[i]+" */\n");
                ReferenceIterator references=currentProgram.getReferenceManager().getReferencesTo(toAddr(args[i]));
                out.write("/* References:");
                while(references.hasNext()) {
                    Reference reference=references.next();
                    out.write(" "+reference.getFromAddress()+"("+reference.getReferenceType()+")");
                }
                out.write(" */\n");
                // Entry points come from the verified lift. The old project
                // hit its analysis timeout; define missing entries only in
                // this -readOnly session (discarded by analyzeHeadless).
                if(function==null) {
                    disassemble(toAddr(args[i]));
                    function=createFunction(toAddr(args[i]),"video_"+args[i]);
                }
                if(function==null){out.write("/* No function at exact entry. */\n");out.flush();continue;}
                out.write("/* Calls:");
                for(Function called:function.getCalledFunctions(monitor))
                    out.write(" "+called.getEntryPoint()+"="+called.getName());
                out.write(" */\n");
                DecompileResults result=decompiler.decompileFunction(function,20,monitor);
                out.write(result.decompileCompleted()?result.getDecompiledFunction().getC():
                    "/* Decompilation failed: "+result.getErrorMessage()+" */\n");
                out.flush(); println("Exported "+args[i]);
            }
        } finally {decompiler.dispose();}
    }
}
