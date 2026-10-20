import java.io.*;
import java.nio.charset.StandardCharsets;

/**
 * Unicode Latin 계열 전체를 텍스트 파일로 출력
 *
 * 출력 범위:
 *   U+0020  ~ U+007E   Basic Latin                    ( 95자)
 *   U+00A0  ~ U+00FF   Latin-1 Supplement             ( 96자)
 *   U+0100  ~ U+017F   Latin Extended-A               (128자)
 *   U+0180  ~ U+024F   Latin Extended-B               (208자)
 *   U+0250  ~ U+02AF   IPA Extensions                 ( 96자)
 *   U+1D00  ~ U+1D7F   Phonetic Extensions            (128자)
 *   U+1D80  ~ U+1DBF   Phonetic Extensions Supplement ( 64자)
 *   U+1E00  ~ U+1EFF   Latin Extended Additional      (256자)
 *   U+2C60  ~ U+2C7F   Latin Extended-C               ( 32자)
 *   U+A720  ~ U+A7FF   Latin Extended-D               (224자)
 *   U+AB30  ~ U+AB6F   Latin Extended-E               ( 64자)
 *   U+FB00  ~ U+FB06   Alphabetic Presentation Forms  (  7자)
 */
public class ExportLatin {

    private static final int CHARS_PER_LINE = 32;
    private static final String OUTPUT_FILE = "latin_all.txt";

    record Range(int start, int end, String name) {}

    static final Range[] RANGES = {
        new Range(0x0020, 0x007E, "Basic Latin"),
        new Range(0x00A0, 0x00FF, "Latin-1 Supplement"),
        new Range(0x0100, 0x017F, "Latin Extended-A"),
        new Range(0x0180, 0x024F, "Latin Extended-B"),
        new Range(0x0250, 0x02AF, "IPA Extensions"),
        new Range(0x1D00, 0x1D7F, "Phonetic Extensions"),
        new Range(0x1D80, 0x1DBF, "Phonetic Extensions Supplement"),
        new Range(0x1E00, 0x1EFF, "Latin Extended Additional"),
        new Range(0x2C60, 0x2C7F, "Latin Extended-C"),
        new Range(0xA720, 0xA7FF, "Latin Extended-D"),
        new Range(0xAB30, 0xAB6F, "Latin Extended-E"),
        new Range(0xFB00, 0xFB06, "Alphabetic Presentation Forms (Latin)"),
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