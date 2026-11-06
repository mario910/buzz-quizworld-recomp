package org.buzzrecomp.game;

import java.io.*;
import java.util.HashSet;
import java.util.Set;
import java.util.zip.ZipEntry;
import java.util.zip.ZipInputStream;

/** Extract only a flat ADPKG into a new private directory, with bounded output. */
final class DriverArchive {
    static final long MAX_BYTES = 256L * 1024 * 1024;
    static void extract(InputStream input, File destination) throws IOException {
        if (!destination.mkdir()) throw new IOException("Nie można utworzyć katalogu sterownika");
        boolean success = false;
        try (ZipInputStream zip = new ZipInputStream(input)) {
            Set<String> names = new HashSet<>();
            long total = 0;
            ZipEntry entry;
            byte[] buffer = new byte[65536];
            while ((entry = zip.getNextEntry()) != null) {
                String name = entry.getName();
                if (entry.isDirectory() || !name.matches("[A-Za-z0-9_+.-]{1,160}") ||
                    name.equals(".") || name.equals("..") || !names.add(name) || names.size() > 64)
                    throw new IOException("Nieprawidłowa struktura ZIP");
                if (!name.equals("meta.json") && !name.endsWith(".so"))
                    throw new IOException("Nieobsługiwany plik: " + name);
                long size = 0;
                try (OutputStream out = new FileOutputStream(new File(destination, name))) {
                    int count;
                    while ((count = zip.read(buffer)) != -1) {
                        total += count; size += count;
                        if (total > MAX_BYTES || (name.equals("meta.json") && size > 65536))
                            throw new IOException("Paczka sterownika jest za duża");
                        out.write(buffer, 0, count);
                    }
                }
                if (name.endsWith(".so")) validateElf(new File(destination, name));
            }
            if (!names.contains("meta.json")) throw new IOException("Brak meta.json");
            success = true;
        } finally { if (!success) remove(destination); }
    }
    static void validateElf(File file) throws IOException {
        byte[] header = new byte[20];
        try (DataInputStream stream = new DataInputStream(new FileInputStream(file))) {
            stream.readFully(header);
        }
        if (header[0] != 0x7f || header[1] != 'E' || header[2] != 'L' || header[3] != 'F' ||
            header[4] != 2 || header[5] != 1 || header[16] != 3 || header[17] != 0 ||
            (header[18] & 255) != 183 || header[19] != 0)
            throw new IOException("Sterownik musi być biblioteką ELF ARM64");
    }
    static void remove(File directory) {
        File[] files = directory.listFiles();
        if (files != null) for (File file : files) file.delete();
        directory.delete();
    }
}
