library(ggplot2)
library(ggtext)
library(ggridges)
library(ggdist)
library(dplyr)
library(sjPlot)
library(readr)
library(tidyr)

# Read the CSV file into a data frame
data <- read.csv("sik-test-data.csv", check.names = FALSE)

# Reshape from wide to long format
data_long <- data %>%
  pivot_longer(
    cols = everything(), 
    names_to = "category", 
    values_to = "duration"
  )

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
    scale = 0.75,
    height = 0.75,
    width = .75, 
    .width = 0,
    justification = -.4,
    alpha = 0.70,
    point_color = NA) + 
  #Boxplot
  geom_boxplot(
    aes(),
    width = .3, 
    outlier.shape = NA
  ) +
  # Median value text
  stat_summary(
    geom = "text",
    fun = "median",
    aes(label = round(..y.., 1)),
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
    hjust = 0.8
  ) +
  coord_flip() +
  # Override x-axis range and ticks
  scale_y_continuous(
    #limits = c(0, 500),
    #breaks = seq(0, 1000, by = 100)
    breaks = pretty(range(data_long$duration, na.rm = TRUE), n = 6, min.n = 4)
    #expand = c(.001, .001)
  ) +
  # Axis Labels
        labs(
          x = NULL,
          y = "Duration (milliseconds)",
          title = "SiK Transfer Durations",
          subtitle = "Defaults: 57600 8N1, 64k, 20dBm",
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
          axis.text.y = element_text(face = "bold")          
          #panel.grid.minor = element_blank(),
          #panel.grid.major.y = element_blank(),
          #axis.ticks = element_blank(),
          #axis.text.x = element_text(family = "Roboto Mono"),

        )

p

save_plot("test.svg", fig = p, width=30, height=16)

