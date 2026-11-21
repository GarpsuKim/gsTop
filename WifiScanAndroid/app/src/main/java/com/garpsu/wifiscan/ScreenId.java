package com.garpsu.wifiscan;

import android.app.Activity;
import android.app.Dialog;
import android.content.Context;
import android.graphics.Color;
import android.view.Gravity;
import android.view.View;
import android.view.ViewGroup;
import android.widget.FrameLayout;
import android.widget.TextView;

/**
 * 화면 번호 표시: 화면 맨 아래 검은 줄에 흰 글자 "화면 1010" (왼쪽 정렬).
 * 수정 요청 때 "1010번 화면의 ○○" 처럼 부르기 위한 번호. 번호 목록은 SCREENS.md 참고.
 * 번호 규칙: 1010 부터 10씩. 이후 추가·삽입되는 화면은 그때마다 정해서 붙임.
 */
final class ScreenId {

    private ScreenId() {
    }

    private static int dp(Context c, int v) {
        return (int) (v * c.getResources().getDisplayMetrics().density);
    }

    // 검은 줄 높이
    static int height(Context c) {
        return dp(c, 18);
    }

    // 검은 줄 (가로 전체, 흰 글자, 왼쪽 정렬)
    static TextView label(Context c, String id) {
        TextView t = new TextView(c);
        t.setText("화면 " + id);
        t.setTextSize(10);
        t.setTextColor(Color.WHITE);
        t.setBackgroundColor(Color.BLACK);
        t.setGravity(Gravity.CENTER_VERTICAL | Gravity.START);
        t.setPadding(dp(c, 8), 0, dp(c, 8), 0);
        t.setLayoutParams(new ViewGroup.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, height(c)));
        return t;
    }

    /*
     * 화면 맨 아래에 검은 줄 붙이기 (팝업·별도 화면 공용).
     * 원래 내용 아래쪽에 줄 높이만큼 빈 공간을 만들고, 그 자리에 줄을 놓아서 버튼을 가리지 않음.
     */
    private static void attach(View contentFrame, Context c, String id) {
        if (!(contentFrame instanceof ViewGroup)) {
            return;
        }
        ViewGroup frame = (ViewGroup) contentFrame;
        int h = height(c);
        if (frame.getChildCount() > 0) {
            View root = frame.getChildAt(0);
            root.setPadding(root.getPaddingLeft(), root.getPaddingTop(),
                    root.getPaddingRight(), root.getPaddingBottom() + h);
        }
        frame.addView(label(c, id), new FrameLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT, h, Gravity.BOTTOM));
    }

    // 팝업: show() 한 뒤에 호출
    static <T extends Dialog> T tag(T d, String id) {
        try {
            attach(d.findViewById(android.R.id.content), d.getContext(), id);
        } catch (Exception ignored) {
        }
        return d;
    }

    // 별도 화면(Activity): setContentView() 한 뒤에 호출
    static void tag(Activity a, String id) {
        try {
            attach(a.findViewById(android.R.id.content), a, id);
        } catch (Exception ignored) {
        }
    }
}
