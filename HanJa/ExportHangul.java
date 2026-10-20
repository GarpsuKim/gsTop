import java.io.*;
import java.nio.charset.StandardCharsets;

/**
 * Unicode에 포함된 모든 한글을 텍스트 파일로 출력
 *
 * 출력 범위:
 *   U+1100  ~ U+11FF   한글 자모                    (256자)
 *   U+3130  ~ U+318F   한글 호환 자모               ( 96자)
 *   U+A960  ~ U+A97F   한글 자모 Extended-A         ( 32자)
 *   U+AC00  ~ U+D7A3   한글 음절 완성형 (가~힣)     (11,172자)
 *   U+D7B0  ~ U+D7FF   한글 자모 Extended-B         ( 80자)
 */
public class ExportHangul {

    private static final int CHARS_PER_LINE = 32;
    private static final String OUTPUT_FILE = "hangul_all.txt";

    record Range(int start, int end, String name) {}

    static final Range[] RANGES = {
        new Range(0x1100,  0x11FF,  "한글 자모"),
        new Range(0x3130,  0x318F,  "한글 호환 자모"),
        new Range(0xA960,  0xA97F,  "한글 자모 Extended-A"),
        new Range(0xAC00,  0xD7A3,  "한글 음절 완성형 (가~힣)"),
        new Range(0xD7B0,  0xD7FF,  "한글 자모 Extended-B"),
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
                bw.write(String.format("[ %s ]  U+%04X ~ U+%04X", r.name(), r.start(), r.end()));
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

                if (!line.isEmpty()) {
                    bw.write(line.toString());
                    bw.newLine();
                }

                bw.write(String.format("  → %,d 자%n", rangeCount));
                bw.newLine();
                totalCount += rangeCount;
            }

            bw.write("═".repeat(60));
            bw.newLine();
            bw.write(String.format("총 합계: %,d 자%n", totalCount));
            bw.write("═".repeat(60));
            bw.newLine();

            System.out.printf("완료: %s  (총 %,d 자)%n", OUTPUT_FILE, totalCount);
        }
    }
}