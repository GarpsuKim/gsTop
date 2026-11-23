package com.garpsu.wifiscan;

import android.Manifest;
import android.app.Activity;
import android.content.Intent;
import android.content.pm.PackageManager;
import android.database.Cursor;
import android.graphics.Color;
import android.graphics.Typeface;
import android.os.Bundle;
import android.provider.ContactsContract;
import android.text.InputType;
import android.view.Gravity;
import android.view.View;
import android.view.ViewGroup;
import android.widget.Button;
import android.widget.EditText;
import android.widget.LinearLayout;
import android.widget.ScrollView;
import android.widget.TextView;
import android.widget.Toast;

import java.util.ArrayList;
import java.util.List;

/**
 * 화면 1100: 보호자 등록 (별도 화면, 예전 팝업을 대신함)
 * - ■ 보호자 목록: 보호자를 한 명씩 카드로 추가/삭제 (이름, 전화번호, 연락처에서 선택). 첫 번째 = SOS 전화
 * - 앞으로 보호자별 항목(관계, 알림 받을 채널 등)이나 새 구역(■)을 계속 추가할 수 있게 틀을 나눠 둠
 * 저장 형식은 기존과 같음: sos 설정의 "phones" = "이름 번호, 이름 번호" (SosActivity.entries 가 읽음)
 * 화면을 벗어나면 자동 저장.
 */
public class GuardianActivity extends Activity {

    private static final int REQ_PICK = 21, REQ_CALL = 22;

    // 보호자 카드 한 장 = 입력 칸 묶음 (항목을 늘릴 때 여기에 필드 추가)
    private static class Card {
        LinearLayout box;
        TextView title;
        EditText name, phone, email;
        Button rel; // 관계
    }

    private final List<Card> cards = new ArrayList<>();
    private LinearLayout list;   // 카드들이 들어가는 곳
    private Card pickTarget;     // 연락처에서 고른 값을 넣을 카드

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);

        LinearLayout root = new LinearLayout(this);
        root.setOrientation(LinearLayout.VERTICAL);
        root.setPadding(40, 30, 40, 60);

        TextView title = new TextView(this);
        title.setText("나의 보호자 등록");
        title.setTextSize(22);
        title.setTypeface(null, Typeface.BOLD);
        root.addView(title);

        // ---------- 1) 보호자 목록 ----------
        section(root, "■ 보호자 목록");
        list = new LinearLayout(this);
        list.setOrientation(LinearLayout.VERTICAL);
        root.addView(list);
        for (String[] entry : load()) {
            addCard(entry[0], entry[1], entry[2], entry[3]);
        }
        if (cards.isEmpty()) {
            addCard("", "", "", ""); // 처음엔 빈 카드 한 장
        }
        Button add = new Button(this);
        add.setText("+ 보호자 추가");
        add.setOnClickListener(view -> addCard("", "", "", ""));
        root.addView(add, matchWrap());

        // ---------- 2) (예정) 추가 구역 자리 ----------
        // 예: ■ 보호자 알림 설정, ■ 보호자 앱 연결 … 은 여기 아래에 section(root, "■ …") 으로 추가

        Button close = new Button(this);
        close.setText("저장하고 닫기");
        close.setTypeface(null, Typeface.BOLD);
        close.setOnClickListener(view -> finish()); // onPause 에서 저장
        LinearLayout.LayoutParams lp = matchWrap();
        lp.topMargin = 60;
        root.addView(close, lp);

        ScrollView sv = new ScrollView(this);
        sv.addView(root);
        setContentView(sv);
        ScreenId.tag(this, "1100"); // 화면 번호
    }

    @Override
    protected void onPause() {
        super.onPause();
        save(); // 화면을 벗어나면 자동 저장
    }

    // ---------- 보호자 카드 ----------

    private void addCard(String name, String phone, String email, String relation) {
        final Card card = new Card();
        card.box = new LinearLayout(this);
        card.box.setOrientation(LinearLayout.VERTICAL);
        card.box.setPadding(30, 20, 30, 20);
        card.box.setBackgroundColor(0x0F000000); // 카드 구분용 옅은 배경

        card.title = new TextView(this);
        card.title.setTextSize(15);
        card.title.setTypeface(null, Typeface.BOLD);
        card.box.addView(card.title);

        card.name = field(card.box, "이름 (예: 엄마)", name, InputType.TYPE_CLASS_TEXT);
        TextView rl = new TextView(this);
        rl.setText("관계");
        rl.setTextSize(12);
        rl.setTextColor(Color.GRAY);
        rl.setPadding(0, 10, 0, 0);
        card.box.addView(rl);
        card.rel = relationButton(this, relation, null); // 화면을 벗어날 때 저장됨
        card.box.addView(card.rel, matchWrap());
        card.phone = field(card.box, "전화번호", phone, InputType.TYPE_CLASS_PHONE);
        card.email = field(card.box, "이메일 (이메일로 위치 요청·회신)", email,
                InputType.TYPE_CLASS_TEXT | InputType.TYPE_TEXT_VARIATION_EMAIL_ADDRESS);

        LinearLayout row = new LinearLayout(this);
        row.setOrientation(LinearLayout.HORIZONTAL);
        Button pick = new Button(this);
        pick.setText("연락처에서 선택");
        pick.setTextSize(12);
        pick.setOnClickListener(view -> {
            pickTarget = card;
            try {
                startActivityForResult(new Intent(Intent.ACTION_PICK,
                        ContactsContract.CommonDataKinds.Phone.CONTENT_URI), REQ_PICK);
            } catch (Exception e) {
                Toast.makeText(this, "연락처를 열 수 없습니다.", Toast.LENGTH_SHORT).show();
            }
        });
        row.addView(pick, new LinearLayout.LayoutParams(0, ViewGroup.LayoutParams.WRAP_CONTENT, 1f));
        // [테스트]: 보호자에게 등록 안내 (이메일 자동 + 문자는 문자 앱에서 [전송])
        Button test = new Button(this);
        test.setText("테스트");
        test.setTextSize(12);
        test.setOnClickListener(view -> notifyGuardian(card, false));
        row.addView(test, new LinearLayout.LayoutParams(0, ViewGroup.LayoutParams.WRAP_CONTENT, 1f));
        // [삭제]: 확인(1101) 후 해제 안내 (이메일 + 문자) 보내고 카드 삭제
        Button del = new Button(this);
        del.setText("삭제");
        del.setTextSize(12);
        del.setOnClickListener(view -> ScreenId.tag(new android.app.AlertDialog.Builder(this)
                .setTitle("보호자 삭제")
                .setMessage(guardianName(card) + "님을 보호자에서 삭제할까요?\n"
                        + "삭제 안내를 이메일로 보내고, 문자 앱을 엽니다. (등록된 것만)")
                .setPositiveButton("삭제", (dialog, which) -> {
                    notifyGuardian(card, true);
                    cards.remove(card);
                    list.removeView(card.box);
                    renumber();
                    save(); // 바로 저장 (삭제된 보호자의 위치 요청에는 더 이상 회신 안 함)
                })
                .setNegativeButton("취소", null)
                .show(), "1101"));
        row.addView(del, new LinearLayout.LayoutParams(0, ViewGroup.LayoutParams.WRAP_CONTENT, 1f));
        card.box.addView(row);

        LinearLayout.LayoutParams lp = matchWrap();
        lp.topMargin = 20;
        list.addView(card.box, lp);
        cards.add(card);
        renumber();
    }

    // ---------- 보호자에게 안내: 이메일(사용자 Gmail 로 자동 발송) + 문자(문자 앱을 열어 수동 [전송]) ----------

    private String guardianName(Card card) {
        String nm = card.name.getText().toString().trim();
        return nm.isEmpty() ? "보호자" : nm;
    }

    // removed=false: [테스트] 등록 안내, true: [삭제] 해제 안내
    private void notifyGuardian(Card card, boolean removed) {
        final String gname = guardianName(card);
        String me = TelegramSender.senderName(this);
        final String meName = me.isEmpty() ? "사용자" : me;
        android.content.SharedPreferences user = getSharedPreferences(UserActivity.PREF_USER, MODE_PRIVATE);
        final String from = user.getString("mail", "").trim();
        final String pass = user.getString("mailPass", "");
        final String mail = card.email.getText().toString().trim();
        final String phone = card.phone.getText().toString().trim();
        boolean mailOk = android.util.Patterns.EMAIL_ADDRESS.matcher(mail).matches();
        boolean phoneOk = phone.replaceAll("[^0-9]", "").length() >= 3;
        if (!mailOk && !phoneOk) {
            Toast.makeText(this, "이메일이나 전화번호를 먼저 입력하세요.", Toast.LENGTH_SHORT).show();
            return;
        }

        final String subject;
        final String body;
        final String sms;
        if (!removed) {
            subject = "[집에왔어요] 보호자 등록 안내 (테스트)";
            body = "안녕하세요, " + gname + "님.\n"
                    + meName + "님이 '집에왔어요' 앱에 " + gname + "님을 보호자로 등록했습니다.\n\n"
                    + (from.isEmpty() ? ""
                    : "■ 위치가 궁금할 때\n   " + from + " 로 제목 #where 메일을 보내면, 앱이 현재 위치를 이메일로 회신합니다.\n\n")
                    + "■ 귀가·외출·SOS 알림을 이 주소로 받을 수 있습니다.\n\n"
                    + "이 메일은 연결 확인용 테스트입니다.";
            sms = "[집에왔어요] " + meName + "님이 " + gname + "님을 보호자로 등록했습니다."
                    + (from.isEmpty() ? "" : " 위치가 궁금하면 " + from + " 로 제목 #where 메일을 보내세요.")
                    + " (테스트)";
        } else {
            subject = "[집에왔어요] 보호자 등록 해제 안내";
            body = "안녕하세요, " + gname + "님.\n"
                    + meName + "님이 '집에왔어요' 앱의 보호자 목록에서 " + gname + "님을 삭제했습니다.\n"
                    + "앞으로 위치 요청(#where)에 회신하지 않으며, 알림도 보내지 않습니다.";
            sms = "[집에왔어요] " + meName + "님이 보호자 목록에서 " + gname + "님을 삭제했습니다. "
                    + "앞으로 위치 요청에 회신하지 않습니다.";
        }

        // 이메일: 사용자 등록(1075)의 Gmail 로 자동 발송 (결과는 알림 메시지로)
        if (mailOk) {
            if (from.isEmpty() || pass.isEmpty()) {
                Toast.makeText(this, "사용자 등록에 발신 Gmail 이 없어 이메일은 보내지 못했습니다.", Toast.LENGTH_LONG).show();
            } else {
                final android.content.Context app = getApplicationContext();
                new Thread(() -> {
                    String err = EmailSender.send(from, pass, mail, subject, body);
                    runOnUiThread(() -> Toast.makeText(app, err == null
                            ? "이메일을 보냈습니다. (" + mail + ")" : "이메일 보내기 실패: " + err, Toast.LENGTH_LONG).show());
                }).start();
            }
        }
        // 문자: 내용이 채워진 문자 앱을 열기 (사용자가 [전송])
        if (phoneOk) {
            try {
                startActivity(HomeService.smsCompose(phone, sms));
            } catch (Exception e) {
                Toast.makeText(this, "문자 앱을 열 수 없습니다.", Toast.LENGTH_SHORT).show();
            }
        }
    }

    // 카드 제목: "보호자 1 (SOS 전화)", "보호자 2" …
    private void renumber() {
        for (int i = 0; i < cards.size(); i++) {
            cards.get(i).title.setText("보호자 " + (i + 1) + (i == 0 ? "  (SOS 때 이 번호로 전화)" : ""));
            cards.get(i).title.setTextColor(i == 0 ? 0xFFC62828 : Color.BLACK);
        }
    }

    // ---------- 불러오기: "guardians"(이름·번호·이메일 JSON) 우선, 없으면 예전 "phones" ----------

    private List<String[]> load() {
        List<String[]> rows = new ArrayList<>();
        String js = SosActivity.prefs(this).getString("guardians", "");
        if (!js.isEmpty()) {
            try {
                org.json.JSONArray arr = new org.json.JSONArray(js);
                for (int i = 0; i < arr.length(); i++) {
                    org.json.JSONObject item = arr.getJSONObject(i);
                    rows.add(new String[]{item.optString("name"), item.optString("phone"), item.optString("email"),
                            item.optString("rel")});
                }
                return rows;
            } catch (Exception ignored) {
                rows.clear();
            }
        }
        for (String[] entry : SosActivity.entries(this)) {
            rows.add(new String[]{entry[0], entry[1], "", ""});
        }
        return rows;
    }

    // ---------- 저장 ----------
    // "guardians" = 카드 전체(JSON), "phones" = "이름 번호, 이름 번호"(SOS·전화용, 기존 형식),
    // "emails" = "a@x.com, b@y.com"(이메일 위치 요청을 받아 줄 주소)

    private void save() {
        StringBuilder sb = new StringBuilder(), mails = new StringBuilder();
        org.json.JSONArray all = new org.json.JSONArray();
        int skipped = 0;
        for (Card card : cards) {
            String num = card.phone.getText().toString().trim();
            String nm = card.name.getText().toString().replaceAll("[,;0-9+]", "").trim();
            String em = card.email.getText().toString().trim();
            boolean phoneOk = num.replaceAll("[^0-9]", "").length() >= 3;
            boolean mailOk = android.util.Patterns.EMAIL_ADDRESS.matcher(em).matches();
            if (!phoneOk && !mailOk) {
                if (!num.isEmpty() || !nm.isEmpty() || !em.isEmpty()) {
                    skipped++; // 전화번호도 이메일도 제대로 없는 카드는 저장 안 함
                }
                continue;
            }
            try {
                all.put(new org.json.JSONObject().put("name", nm).put("phone", phoneOk ? num : "")
                        .put("email", mailOk ? em : "").put("rel", relationOf(card.rel)));
            } catch (Exception ignored) {
            }
            if (phoneOk) {
                sb.append(sb.length() > 0 ? ", " : "").append(nm.isEmpty() ? num : nm + " " + num);
            }
            if (mailOk) {
                mails.append(mails.length() > 0 ? ", " : "").append(em);
            }
        }
        SosActivity.prefs(this).edit().putString("phones", sb.toString())
                .putString("emails", mails.toString()).putString("guardians", all.toString()).apply();
        HomeService.refresh(this); // 보호자 이메일이 생기거나 없어지면 이메일 위치 요청 감시 시작·중지
        if (skipped > 0) {
            Toast.makeText(this, "전화번호·이메일이 없는 보호자 " + skipped + "명은 저장하지 않았습니다.",
                    Toast.LENGTH_LONG).show();
        }
        // 보호자가 있으면 '전화 걸기' 권한을 한 번만 요청 (거절하면 전화 화면까지만 띄우는 방식으로 동작)
        if (sb.length() > 0 && checkSelfPermission(Manifest.permission.CALL_PHONE) != PackageManager.PERMISSION_GRANTED
                && !SosActivity.prefs(this).getBoolean("askedCall", false)) {
            SosActivity.prefs(this).edit().putBoolean("askedCall", true).apply();
            requestPermissions(new String[]{Manifest.permission.CALL_PHONE}, REQ_CALL);
        }
    }

    // ---------- 연락처에서 고르기 (이름 + 번호) ----------

    @Override
    protected void onActivityResult(int req, int result, Intent data) {
        super.onActivityResult(req, result, data);
        if (req != REQ_PICK || result != RESULT_OK || data == null || data.getData() == null || pickTarget == null) {
            return;
        }
        try (Cursor cursor = getContentResolver().query(data.getData(), new String[]{
                ContactsContract.CommonDataKinds.Phone.NUMBER,
                ContactsContract.CommonDataKinds.Phone.DISPLAY_NAME}, null, null, null)) {
            if (cursor != null && cursor.moveToFirst()) {
                pickTarget.phone.setText(cursor.getString(0));
                String nm = cursor.getString(1);
                if (nm != null && pickTarget.name.getText().toString().trim().isEmpty()) {
                    pickTarget.name.setText(nm);
                }
            }
        } catch (Exception e) {
            Toast.makeText(this, "연락처를 읽을 수 없습니다.", Toast.LENGTH_SHORT).show();
        }
    }

    // ---------- 관계 (1100 보호자 · 1058 피보호자 공용) ----------
    // 리스트 박스(Spinner)는 기기 테마·다크 모드에 따라 글자가 안 보여서,
    // 앱의 다른 버튼과 같은 [버튼] + 고르기 팝업(화면 1102) 방식으로 함

    static final String[] RELATIONS = {"부모", "조부모", "배우자", "선생님(담임)", "선생님(일반)", "친지어르신",
            "형제자매", "친구", "연인", "자식", "조카", "의료진", "제자", "기타"};

    // [관계 버튼]: 누르면 목록 팝업, 고르면 버튼 글자가 바뀜. onChange 는 고른 뒤 할 일 (없으면 null)
    static Button relationButton(final android.app.Activity activity, String value, final Runnable onChange) {
        final Button btn = new Button(activity);
        btn.setAllCaps(false);
        btn.setTextSize(14);
        btn.setGravity(Gravity.START | Gravity.CENTER_VERTICAL);
        setRelation(btn, value);
        btn.setOnClickListener(view -> {
            String[] items = new String[RELATIONS.length + 1];
            items[0] = "(선택 안 함)";
            System.arraycopy(RELATIONS, 0, items, 1, RELATIONS.length);
            int cur = 0;
            for (int i = 0; i < RELATIONS.length; i++) {
                if (RELATIONS[i].equals(relationOf(btn))) {
                    cur = i + 1;
                }
            }
            ScreenId.tag(new android.app.AlertDialog.Builder(activity)
                    .setTitle("관계")
                    .setSingleChoiceItems(items, cur, (dialog, which) -> {
                        setRelation(btn, which == 0 ? "" : RELATIONS[which - 1]);
                        dialog.dismiss();
                        if (onChange != null) {
                            onChange.run();
                        }
                    })
                    .setNegativeButton("취소", null)
                    .show(), "1102");
        });
        return btn;
    }

    static void setRelation(Button btn, String value) {
        btn.setTag(value == null ? "" : value);
        btn.setText((value == null || value.isEmpty() ? "관계 선택" : value) + "   ▼");
    }

    static String relationOf(Button btn) {
        Object tag = btn.getTag();
        return tag == null ? "" : tag.toString();
    }

    // ---------- 화면 도우미 ----------

    private void section(LinearLayout root, String heading) {
        TextView tv = new TextView(this);
        tv.setText(heading);
        tv.setTextSize(17);
        tv.setTypeface(null, Typeface.BOLD);
        tv.setTextColor(0xFF1976D2);
        tv.setPadding(0, 44, 0, 6);
        root.addView(tv);
    }

    private EditText field(LinearLayout parent, String label, String value, int type) {
        TextView caption = new TextView(this);
        caption.setText(label);
        caption.setTextSize(12);
        caption.setTextColor(Color.GRAY);
        caption.setPadding(0, 10, 0, 0);
        parent.addView(caption);
        EditText edit = new EditText(this);
        edit.setSingleLine(true);
        edit.setInputType(type);
        edit.setText(value);
        edit.setGravity(Gravity.START);
        parent.addView(edit);
        return edit;
    }

    private static LinearLayout.LayoutParams matchWrap() {
        return new LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT);
    }
}
