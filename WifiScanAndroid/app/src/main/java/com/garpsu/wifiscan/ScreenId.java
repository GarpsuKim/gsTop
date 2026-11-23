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

    private static int dp(Context ctx, int value) {
        return (int) (value * ctx.getResources().getDisplayMetrics().density);
    }

    // 검은 줄 높이
    static int height(Context ctx) {
        return dp(ctx, 18);
    }

    // 검은 줄 (가로 전체, 흰 글자, 왼쪽 정렬)
    static TextView label(Context ctx, String id) {
        TextView tv = new TextView(ctx);
        tv.setText("화면 " + id);
        tv.setTextSize(10);
        tv.setTextColor(Color.WHITE);
        tv.setBackgroundColor(Color.BLACK);
        tv.setGravity(Gravity.CENTER_VERTICAL | Gravity.START);
        tv.setPadding(dp(ctx, 8), 0, dp(ctx, 8), 0);
        tv.setLayoutParams(new ViewGroup.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, height(ctx)));
        return tv;
    }

    /*
     * 화면 맨 아래에 검은 줄 붙이기 (팝업·별도 화면 공용).
     * 원래 내용 아래쪽에 줄 높이만큼 빈 공간을 만들고, 그 자리에 줄을 놓아서 버튼을 가리지 않음.
     */
    private static void attach(View contentFrame, Context ctx, String id) {
        if (!(contentFrame instanceof ViewGroup)) {
            return;
        }
        ViewGroup frame = (ViewGroup) contentFrame;
        int px = height(ctx);
        if (frame.getChildCount() > 0) {
            View root = frame.getChildAt(0);
            root.setPadding(root.getPaddingLeft(), root.getPaddingTop(),
                    root.getPaddingRight(), root.getPaddingBottom() + px);
        }
        frame.addView(label(ctx, id), new FrameLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT, px, Gravity.BOTTOM));
    }

    // 팝업: show() 한 뒤에 호출
    static <T extends Dialog> T tag(T dialog, String id) {
        try {
            attach(dialog.findViewById(android.R.id.content), dialog.getContext(), id);
        } catch (Exception ignored) {
        }
        return dialog;
    }

    // 별도 화면(Activity): setContentView() 한 뒤에 호출
    static void tag(Activity activity, String id) {
        try {
            attach(activity.findViewById(android.R.id.content), activity, id);
        } catch (Exception ignored) {
        }
    }
}
