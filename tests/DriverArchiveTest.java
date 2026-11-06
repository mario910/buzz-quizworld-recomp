package org.buzzrecomp.game;
import java.io.*;
import java.nio.file.*;
import java.util.zip.*;

public final class DriverArchiveTest {
    private static byte[] archive(String[] names, byte[][] contents) throws Exception {
        ByteArrayOutputStream bytes = new ByteArrayOutputStream();
        try (ZipOutputStream zip = new ZipOutputStream(bytes)) {
            for (int i=0;i<names.length;i++) { zip.putNextEntry(new ZipEntry(names[i])); zip.write(contents[i]); zip.closeEntry(); }
        }
        return bytes.toByteArray();
    }
    private static void rejected(File root, String name, byte[] zip) throws Exception {
        File target = new File(root, name);
        try { DriverArchive.extract(new ByteArrayInputStream(zip), target); throw new AssertionError(name); }
        catch (IOException expected) { if (target.exists()) throw new AssertionError("Partial import: " + name); }
    }
    public static void main(String[] args) throws Exception {
        File root = Files.createTempDirectory("buzz-driver-test").toFile();
        byte[] meta = "{}".getBytes("UTF-8");
        byte[] elf = new byte[64]; elf[0]=127;elf[1]='E';elf[2]='L';elf[3]='F';elf[4]=2;elf[5]=1;elf[16]=3;elf[18]=(byte)183;
        File valid = new File(root, "valid");
        DriverArchive.extract(new ByteArrayInputStream(archive(new String[]{"meta.json","vulkan.test.so"},new byte[][]{meta,elf})), valid);
        if (!new File(valid,"vulkan.test.so").isFile()) throw new AssertionError();
        DriverArchive.remove(valid);
        rejected(root,"traversal",archive(new String[]{"../escape.so"},new byte[][]{elf}));
        rejected(root,"absolute",archive(new String[]{"/escape.so"},new byte[][]{elf}));
        rejected(root,"windows",archive(new String[]{"..\\escape.so"},new byte[][]{elf}));
        rejected(root,"nested",archive(new String[]{"dir/driver.so"},new byte[][]{elf}));
        rejected(root,"wrong-arch",archive(new String[]{"driver.so"},new byte[][]{new byte[64]}));
        rejected(root,"no-meta",archive(new String[]{"driver.so"},new byte[][]{elf}));
        rejected(root,"large-meta",archive(new String[]{"meta.json"},new byte[][]{new byte[65537]}));
        rejected(root,"unexpected",archive(new String[]{"script.sh"},new byte[][]{elf}));
        byte[] duplicate = archive(new String[]{"meta.json","metb.json"},new byte[][]{meta,meta});
        for (int i=0;i<duplicate.length-9;i++) if(new String(duplicate,i,9,"ISO-8859-1").equals("metb.json")) duplicate[i+3]='a';
        rejected(root,"duplicate",duplicate);
        // ZIP bombs are limited by actual inflated bytes, regardless of headers.
        ByteArrayOutputStream bytes = new ByteArrayOutputStream();
        try (ZipOutputStream zip = new ZipOutputStream(bytes)) {
            zip.putNextEntry(new ZipEntry("driver.so")); byte[] zeros = new byte[65536];
            for(int i=0;i<=DriverArchive.MAX_BYTES/zeros.length;i++) zip.write(zeros);
        }
        rejected(root,"inflate-limit",bytes.toByteArray());
        root.delete();
        System.out.println("Driver ZIP: valid ARM64, traversal, duplicates, wrong ELF, metadata and size limits PASS");
    }
}
