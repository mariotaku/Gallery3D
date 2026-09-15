package me.mariotaku.gallery3d;

import android.app.Activity;
import android.os.Bundle;
import android.util.TypedValue;
import android.view.View;
import android.widget.LinearLayout;
import android.widget.RadioButton;
import android.widget.RadioGroup;
import android.widget.ScrollView;
import android.widget.TextView;

/**
 * Picks the network FakeCloudProvider answers as: fast home network, 5G, 4G,
 * bad 4G or offline. The choice holds across restarts and takes effect on the
 * provider's next call. Folders already listed stay on the wall, so choose
 * the source again to walk the tree on the new network.
 */
public class FakeCloudNetworkActivity extends Activity {

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        final int padding = (int) TypedValue.applyDimension(TypedValue.COMPLEX_UNIT_DIP, 16,
                getResources().getDisplayMetrics());

        LinearLayout column = new LinearLayout(this);
        column.setOrientation(LinearLayout.VERTICAL);
        column.setPadding(padding, padding, padding, padding);

        TextView heading = new TextView(this);
        heading.setText("Fake cloud network");
        heading.setTextSize(TypedValue.COMPLEX_UNIT_SP, 22);
        column.addView(heading);

        RadioGroup networks = new RadioGroup(this);
        final FakeCloudProvider.Network chosen = FakeCloudProvider.chosenNetwork(this);
        for (FakeCloudProvider.Network network : FakeCloudProvider.Network.values()) {
            RadioButton button = new RadioButton(this);
            button.setId(View.generateViewId());
            button.setText(network.title + "\n" + network.describe());
            button.setPadding(0, padding / 2, 0, padding / 2);
            button.setTag(network);
            networks.addView(button);
            if (network == chosen) {
                networks.check(button.getId());
            }
        }
        networks.setOnCheckedChangeListener((group, checkedId) -> {
            final View button = group.findViewById(checkedId);
            if (button != null) {
                FakeCloudProvider.chooseNetwork(this, (FakeCloudProvider.Network) button.getTag());
            }
        });
        column.addView(networks);

        ScrollView scroll = new ScrollView(this);
        scroll.setFitsSystemWindows(true);
        scroll.addView(column);
        setContentView(scroll);
    }
}
