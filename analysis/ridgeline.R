library(ggplot2)
library(ggtext)
library(ggridges)
library(ggdist)
library(dplyr)
library(sjPlot)
library(readr)
library(tidyr)

# Read the CSV file into a data frame
data <- read.csv("esp32-nimble-results.csv", 
                 check.names = FALSE, 
                 stringsAsFactors = FALSE)

column_order <- names(data)

# Reshape from wide to long format
data_long <- data %>%
  pivot_longer(
    cols = everything(), 
    names_to = "category", 
    values_to = "duration"
  )

# Convert 'category' to a factor with levels defined by column order from the CSV
data_long$category <- factor(data_long$category, levels = column_order)
# data_long$category <- factor(data_long$category, levels = rev(column_order))

# Convert to microseconds
# data_long$duration <- data_long$duration/1e3

# Sum the number of samples to show as n=123 text
add_sample <- function(x){
  return(c(y = max(x) + .025, 
           label = length(x)))
}

# Main Chart
p <- data_long %>% 
  filter(!is.na(duration)) %>%
  ggplot(aes(x = category, y = duration)) + 
  # Distribution curve
  ggdist::stat_halfeye(
    aes(),
    adjust = .3, 
    normalize = "groups",
    scale = 0.75,
    height = 0.75,
    width = .5, 
    .width = 0,
    justification = -.5,
    alpha = 0.70,
    na.rm = TRUE,
    point_color = NA 
  ) + 
  #Boxplot
  geom_boxplot(
    aes(),
    width = .3,
    outlier.colour = 'black',
    outlier.alpha = 0.6,
    outlier.size = 0.4,
  ) +
  # Median value text
  stat_summary(
    geom = "text",
    fun = "median",
    aes(label = round(..y.., 2)),
    family = "Roboto Mono",
    fontface = "bold",
    size = 5,
    vjust = 2.75
  ) +
  # Sample Count text
  stat_summary(
    geom = "text",
    fun.data = add_sample,
    aes(label = paste("n =", ..label..)),
    family = "Roboto Condensed",
    size = 4,
    vjust = 1.5,
    hjust = -0.4
  ) +
  coord_flip( ylim = c(0, NA), clip = "off" ) +
  # Override x-axis range and ticks
  scale_y_continuous(
    limits = c(0, 195),
    breaks = seq(0, 1500, by = 20)
    # breaks = pretty(range(data_long$duration, na.rm = TRUE), n = 6, min.n = 4),
    #expand = c(0.05, 1)
  ) +
  # Axis Labels
        labs(
          x = NULL,
          y = "Duration (milliseconds)",
          title = "ESP32 NimBLE GATT Transfer Durations",
          subtitle = "ESP-IDF 5.1.1, NimBLE, MTU=200, Notify",
          caption = "Lower is better"
        ) +
        theme_minimal() +
        theme(
          text = element_text(family = "Roboto Mono", size = 20, face = "plain"),
          legend.position = "none",
          plot.margin = margin(5, 5, 5, 5),
          plot.title = element_text(hjust = 0.5),    # Center the title
          plot.subtitle = element_text(hjust = 0.5, size = 16), # Center the subtitle
          plot.caption = element_text(hjust = 0.5),   # Center the caption
          axis.text.x = element_text(margin = margin(t = 5, r = 0, b = 15, l = 0)), # +Gap between axis and label
          axis.text.y = element_text(face = "bold"),
          #axis.line.y = element_line(color = "grey", size = 1) # Adjust line thickness
          #panel.grid.minor = element_blank(),
          #panel.grid.major.y = element_blank(),
          #axis.ticks = element_blank(),
          #axis.text.x = element_text(family = "Roboto Mono"),

        )

p

save_plot("test.svg", fig = p, width=30, height=14)

