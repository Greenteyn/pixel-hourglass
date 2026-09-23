// config.js — the Clay settings page shown on the phone. Clay relays the chosen
// value to the watch itself; index.js only starts it.
module.exports = [
    {
        type: "heading",
        defaultValue: "Pixel Hourglass",
    },
    {
        type: "select",
        messageKey: "CyclePeriod",
        label: "Time for the sand to run out",
        description:
            "The hourglass turns over when the sand runs out, and starts " +
            "again from full." +
            "<br><br>" +
            "On the one-minute setting the sand moves every few seconds; " +
            "on the longest it stands still for hours. Every move costs " +
            "a little battery." +
            "<br><br>" +
            "Cycles are counted from midnight on your watch, not from the " +
            "moment you save, so a new choice does not turn the hourglass " +
            "over.",
        defaultValue: "3600",
        options: [
            { label: "1 minute", value: "60" },
            { label: "10 minutes", value: "600" },
            { label: "30 minutes", value: "1800" },
            { label: "1 hour", value: "3600" },
            { label: "12 hours", value: "43200" },
            { label: "24 hours", value: "86400" },
        ],
    },
    {
        type: "submit",
        defaultValue: "Save",
    },
];
