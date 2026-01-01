package com.termux.shared.interact;

import android.content.Context;
import android.content.DialogInterface;
import android.graphics.Color;
import android.view.LayoutInflater;
import android.view.View;
import android.widget.Button;
import android.widget.TextView;

import androidx.appcompat.app.AlertDialog;

import com.termux.shared.R;
import com.termux.shared.logger.Logger;

public class MessageDialogUtils {

    public static void showMessage(Context context, String titleText, String messageText, final DialogInterface.OnDismissListener onDismiss) {
        showMessage(context, titleText, messageText, null, null, null, null, onDismiss);
    }

    public static void showMessage(Context context, String titleText, String messageText,
                                   String positiveText,
                                   final DialogInterface.OnClickListener onPositiveButton,
                                   String negativeText,
                                   final DialogInterface.OnClickListener onNegativeButton,
                                   final DialogInterface.OnDismissListener onDismiss) {
        AlertDialog.Builder builder = new AlertDialog.Builder(context, androidx.appcompat.R.style.Theme_AppCompat_Light_Dialog);
        LayoutInflater inflater = (LayoutInflater) context.getSystemService( Context.LAYOUT_INFLATER_SERVICE );
        View view = inflater.inflate(R.layout.dialog_show_message, null);
        if (view != null) {
            builder.setView(view);
            TextView titleView = view.findViewById(R.id.dialog_title);
            if (titleView != null)
                titleView.setText(titleText);
            TextView messageView = view.findViewById(R.id.dialog_message);
            if (messageView != null)
                messageView.setText(messageText);
        }
        if (positiveText == null)
            positiveText = context.getString(android.R.string.ok);
        builder.setPositiveButton(positiveText, onPositiveButton);
        if (negativeText != null)
            builder.setNegativeButton(negativeText, onNegativeButton);
        if (onDismiss != null)
            builder.setOnDismissListener(onDismiss);
        AlertDialog dialog = builder.create();
        dialog.setOnShowListener(dialogInterface -> {
            Logger.logError("dialog");
            Button button = dialog.getButton(AlertDialog.BUTTON_POSITIVE);
            if (button != null)
                button.setTextColor(Color.BLACK);
            button = dialog.getButton(AlertDialog.BUTTON_NEGATIVE);
            if (button != null)
                button.setTextColor(Color.BLACK);
        });
        dialog.show();
    }

    public static void exitAppWithErrorMessage(Context context, String titleText, String messageText) {
        showMessage(context, titleText, messageText, dialog -> System.exit(0));
    }
}
