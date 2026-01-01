package com.termux.shared.view;

import android.app.Activity;
import android.content.Context;
import android.content.ContextWrapper;
import android.content.res.Configuration;
import android.graphics.Point;
import android.graphics.Rect;
import android.os.Build;
import android.util.TypedValue;
import android.view.Display;
import android.view.View;
import android.view.ViewGroup;
import android.view.WindowManager;
import android.view.WindowMetrics;

import androidx.annotation.NonNull;
import androidx.annotation.Nullable;
import androidx.appcompat.app.ActionBar;
import androidx.appcompat.app.AppCompatActivity;

import com.termux.shared.logger.Logger;

public class ViewUtils {

    public static boolean VIEW_UTILS_LOGGING_ENABLED = false;

    private static final String LOG_TAG = "ViewUtils";

    public static void setIsViewUtilsLoggingEnabled(boolean value) {
        VIEW_UTILS_LOGGING_ENABLED = value;
    }

    public static boolean isViewFullyVisible(View view, int statusBarHeight) {
        Rect[] windowAndViewRects = getWindowAndViewRects(view, statusBarHeight);
        if (windowAndViewRects == null)
            return false;
        return windowAndViewRects[0].contains(windowAndViewRects[1]);
    }

    @Nullable
    public static Rect[] getWindowAndViewRects(View view, int statusBarHeight) {
        if (view == null || !view.isShown())
            return null;

        boolean view_utils_logging_enabled = VIEW_UTILS_LOGGING_ENABLED;

        Rect windowRect = new Rect();
        view.getWindowVisibleDisplayFrame(windowRect);

        int actionBarHeight = 0;
        boolean isInMultiWindowMode = false;
        Context context = view.getContext();
        if (context instanceof AppCompatActivity) {
            ActionBar actionBar = ((AppCompatActivity) context).getSupportActionBar();
            if (actionBar != null) actionBarHeight = actionBar.getHeight();
            isInMultiWindowMode = (Build.VERSION.SDK_INT >= Build.VERSION_CODES.N) && ((AppCompatActivity) context).isInMultiWindowMode();
        } else if (context instanceof Activity) {
            android.app.ActionBar actionBar = ((Activity) context).getActionBar();
            if (actionBar != null) actionBarHeight = actionBar.getHeight();
            isInMultiWindowMode = (Build.VERSION.SDK_INT >= Build.VERSION_CODES.N) && ((Activity) context).isInMultiWindowMode();
        }

        int displayOrientation = getDisplayOrientation(context);

        Rect windowAvailableRect;
        windowAvailableRect = new Rect(windowRect.left, windowRect.top + actionBarHeight, windowRect.right, windowRect.bottom);

        Rect viewRect;
        final int[] viewsLocationInWindow = new int[2];
        view.getLocationInWindow(viewsLocationInWindow);
        int viewLeft = viewsLocationInWindow[0];
        int viewTop = viewsLocationInWindow[1];

        if (view_utils_logging_enabled) {
            Logger.logVerbose(LOG_TAG, "getWindowAndViewRects:");
            Logger.logVerbose(LOG_TAG, "windowRect: " + toRectString(windowRect) + ", windowAvailableRect: " + toRectString(windowAvailableRect));
            Logger.logVerbose(LOG_TAG, "viewsLocationInWindow: " + toPointString(new Point(viewLeft, viewTop)));
            Logger.logVerbose(LOG_TAG, "activitySize: " + toPointString(getDisplaySize(context, true)) +
                ", displaySize: " + toPointString(getDisplaySize(context, false)) +
                ", displayOrientation=" + displayOrientation);
        }

        if (isInMultiWindowMode) {
            if (displayOrientation == Configuration.ORIENTATION_PORTRAIT) {
                if (statusBarHeight != windowRect.top) {
                    if (view_utils_logging_enabled)
                        Logger.logVerbose(LOG_TAG, "Window top does not equal statusBarHeight " + statusBarHeight + " in multi-window portrait mode. Window is possibly bottom app in split screen mode. Adding windowRect.top " + windowRect.top + " to viewTop.");
                    viewTop += windowRect.top;
                } else {
                    if (view_utils_logging_enabled)
                        Logger.logVerbose(LOG_TAG, "windowRect.top equals statusBarHeight " + statusBarHeight + " in multi-window portrait mode. Window is possibly top app in split screen mode.");
                }

            } else if (displayOrientation == Configuration.ORIENTATION_LANDSCAPE) {
                viewLeft += windowRect.left;
            }
        }

        int viewRight = viewLeft + view.getWidth();
        int viewBottom = viewTop + view.getHeight();
        viewRect = new Rect(viewLeft, viewTop, viewRight, viewBottom);

        if (displayOrientation == Configuration.ORIENTATION_LANDSCAPE && viewRight > windowAvailableRect.right) {
            if (view_utils_logging_enabled)
                Logger.logVerbose(LOG_TAG, "viewRight " + viewRight + " is greater than windowAvailableRect.right " + windowAvailableRect.right + " in landscape mode. Setting windowAvailableRect.right to viewRight since it may not include navbar height.");
            windowAvailableRect.right = viewRight;
        }

        return new Rect[]{windowAvailableRect, viewRect};
    }

    public static boolean isRectAbove(@NonNull Rect r1, @NonNull Rect r2) {
        return r1.left < r1.right && r1.top < r1.bottom
            && r1.left <= r2.left && r1.bottom >= r2.bottom;
    }

    public static int getDisplayOrientation(@NonNull Context context) {
        Point size = getDisplaySize(context, false);
        return (size.x < size.y) ? Configuration.ORIENTATION_PORTRAIT : Configuration.ORIENTATION_LANDSCAPE;
    }

    public static Point getDisplaySize(@NonNull Context context, boolean activitySize) {
        WindowManager wm = (WindowManager) context.getSystemService(Context.WINDOW_SERVICE);
        Point point = new Point();

        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R) {
            WindowMetrics metrics = wm.getCurrentWindowMetrics();
            Rect bounds = metrics.getBounds();
            return new Point(bounds.width(), bounds.height());
        } else {
            Display display = wm.getDefaultDisplay();
            display.getRealSize(point);
            return point;
        }
    }

    public static String toRectString(Rect rect) {
        if (rect == null) return "null";
        return "(" + rect.left + "," + rect.top + "), (" + rect.right + "," + rect.bottom + ")";
    }

    public static String toPointString(Point point) {
        if (point == null) return "null";
        return "(" + point.x + "," + point.y + ")";
    }

    @Nullable
    public static Activity getActivity(Context context) {
        while (context instanceof ContextWrapper) {
            if (context instanceof Activity) {
                return (Activity)context;
            }
            context = ((ContextWrapper)context).getBaseContext();
        }
        return null;
    }

    public static float dpToPx(Context context, float dp) {
        return TypedValue.applyDimension(TypedValue.COMPLEX_UNIT_DIP, dp, context.getResources().getDisplayMetrics());
    }

    public static float pxToDp(Context context, float px) {
        return px / context.getResources().getDisplayMetrics().density;
    }

    public static void setLayoutMarginsInDp(@NonNull View view, int left, int top, int right, int bottom) {
        Context context = view.getContext();
        setLayoutMarginsInPixels(view, (int) dpToPx(context, left), (int) dpToPx(context, top),
            (int) dpToPx(context, right), (int) dpToPx(context, bottom));
    }

    public static void setLayoutMarginsInPixels(@NonNull View view, int left, int top, int right, int bottom) {
        if (view.getLayoutParams() instanceof ViewGroup.MarginLayoutParams) {
            ViewGroup.MarginLayoutParams params = (ViewGroup.MarginLayoutParams) view.getLayoutParams();
            params.setMargins(left, top, right, bottom);
            view.setLayoutParams(params);
        }
    }
}
