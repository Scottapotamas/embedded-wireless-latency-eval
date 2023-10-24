# Analysis

Timing captures from Saleae Logic were exported as .csv files (found alongside their test firmwares in this repo).

The `saleae-latency-log-cleanup.R` script is a quick hack to get edge transition timestamps into a set of durations.

Instead of trying to learn R's charting/boxplot tools and syntax, I take this exported file through the BoxPlotR tool.
