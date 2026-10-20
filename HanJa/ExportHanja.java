import java.io.*;
import java.nio.charset.StandardCharsets;

/**
 * PC(Unicode)에 포함된 모든 漢字(CJK)를 텍스트 파일로 출력
 *
 * 출력 범위:
 *   U+3400  ~ U+4DBF   CJK Unified Ideographs Extension A       (6,592자)
 *   U+4E00  ~ U+9FFF   CJK Unified Ideographs (기본)            (20,992자)
 *   U+F900  ~ U+FAFF   CJK Compatibility Ideographs             (  512자)
 *   U+20000 ~ U+2A6DF  CJK Unified Ideographs Extension B       (42,720자)
 *   U+2A700 ~ U+2B73F  CJK Unified Ideographs Extension C       (4,160자)
 *   U+2B740 ~ U+2B81F  CJK Unified Ideographs Extension D       (  224자)
 *   U+2B820 ~ U+2CEAF  CJK Unified Ideographs Extension E       (5,776자)
 *   U+2CEB0 ~ U+2EBEF  CJK Unified Ideographs Extension F       (7,488자)
 *   U+2EBF0 ~ U+2EE5F  CJK Unified Ideographs Extension I       (  624자)
 *   U+30000 ~ U+3134F  CJK Unified Ideographs Extension G       (4,944자)
 *   U+31350 ~ U+323AF  CJK Unified Ideographs Extension H       (4,192자)
 *   U+2F800 ~ U+2FA1F  CJK Compatibility Ideographs Supplement  (  544자)
 */
public class ExportHanja {

    // 한 줄에 출력할 글자 수
    private static final int CHARS_PER_LINE = 32;

    // 출력 파일명
    private static final String OUTPUT_FILE = "hanja_all.txt";

    record Range(int start, int end, String name) {}

    static final Range[] RANGES = {
        new Range(0x3400,  0x4DBF,  "CJK Ext-A"),
        new Range(0x4E00,  0x9FFF,  "CJK Unified Ideographs"),
        new Range(0xF900,  0xFAFF,  "CJK Compatibility Ideographs"),
        new Range(0x20000, 0x2A6DF, "CJK Ext-B"),
        new Range(0x2A700, 0x2B73F, "CJK Ext-C"),
        new Range(0x2B740, 0x2B81F, "CJK Ext-D"),
        new Range(0x2B820, 0x2CEAF, "CJK Ext-E"),
        new Range(0x2CEB0, 0x2EBEF, "CJK Ext-F"),
        new Range(0x2EBF0, 0x2EE5F, "CJK Ext-I"),
        new Range(0x30000, 0x3134F, "CJK Ext-G"),
        new Range(0x31350, 0x323AF, "CJK Ext-H"),
        new Range(0x2F800, 0x2FA1F, "CJK Compatibility Ideographs Supplement"),
    };

    public static void main(String[] args) throws IOException {

        try (BufferedWriter bw = new BufferedWriter(
                new OutputStreamWriter(new FileOutputStream(OUTPUT_FILE), StandardCharsets.UTF_8))) {

            // UTF-8 BOM (Windows 메모장 호환)
            bw.write('\uFEFF');

            int totalCount = 0;

            for (Range r : RANGES) {
                int rangeCount = 0;
                StringBuilder line = new StringBuilder();

                bw.write("═".repeat(60));
                bw.newLine();
                bw.write(String.format("[ %s ]  U+%05X ~ U+%05X", r.name(), r.start(), r.end()));
                bw.newLine();
                bw.write("═".repeat(60));
                bw.newLine();

                for (int cp = r.start(); cp <= r.end(); cp++) {
                    if (!Character.isDefined(cp)) continue;
                    if (Character.getType(cp) == Character.UNASSIGNED) continue;

                    line.appendCodePoint(cp);
                    line.append(' ');
                    rangeCount++;

                    if (rangeCount % CHARS_PER_LINE == 0) {
                        bw.write(line.toString());
                        bw.newLine();
                        line.setLength(0);
                    }
                }

                // 나머지
                if (!line.isEmpty()) {
                    bw.write(line.toString());
                    bw.newLine();
                }

                bw.write(String.format("  → %,d 字%n", rangeCount));
                bw.newLine();
                totalCount += rangeCount;
            }

            bw.write("═".repeat(60));
            bw.newLine();
            bw.write(String.format("총 합계: %,d 字%n", totalCount));
            bw.write("═".repeat(60));
            bw.newLine();

            System.out.printf("완료: %s  (총 %,d 字)%n", OUTPUT_FILE, totalCount);
        }
    }
}