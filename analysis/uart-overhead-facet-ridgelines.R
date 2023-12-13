library(ggplot2)
library(ggtext)
library(ggridges)
library(ggdist)
library(dplyr)
library(sjPlot)
library(readr)
library(tidyr)

# Read the CSV file into a data frame
data <- read.csv("increasing-baudrate-transport-duration-removed.csv", 
                 check.names = FALSE, 
                 stringsAsFactors = FALSE)

# Reshape from wide to long format
data_long <- data %>%
  pivot_longer(
    cols = everything(), 
    names_to = "category", 
    values_to = "duration"
  ) %>%
  # Create the baudrate column by extracting the baud rate
  mutate(baudrate = gsub("-.*", "", category),
         # Create the subgroup column by extracting the type (POLL, IRQ, DMA)
         subgroup = gsub(".*-", "", category)) 

# Convert baudrate to numeric to sort numerically
data_long$baudrate <- as.numeric(as.character(data_long$baudrate))

# Order the baudrate numerically and then convert back to a factor
data_long$baudrate <- factor(data_long$baudrate, levels = sort(unique(data_long$baudrate), decreasing = FALSE))

# Convert to microseconds
data_long$duration <- data_long$duration/1e3


# Custom colours
less_saturated_colors <- c('#b27c7c', '#a7b27c', '#7cb292', '#7c92b2', '#a77cb2')

# Sum the number of samples to show as n=123 text
add_sample <- function(x){
  return(c(y = max(x) + .025, 
           label = length(x)))
}

# Main Chart
p <- data_long %>% 
  filter(!is.na(duration)) %>%
  ggplot(aes(x = subgroup, y = duration, fill = baudrate,)) + 
  # Distribution curve
  ggdist::stat_halfeye(
    aes(),
    adjust = .3, 
    normalize = "groups",
    scale = 0.75,
    height = 0.75,
    width = .4, 
    .width = 0,
    justification = -.8,
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
    vjust = 2.2
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
  
  # Each baudrate should use it's own colour
  # scale_fill_manual(values = less_saturated_colors) +
  # scale_color_manual(values = less_saturated_colors) +
  
  # Separate facet for each baudrate
  # One column layout
  facet_grid(
    baudrate ~ ., 
    scales = "fixed", 
    space = "fixed",
    switch = "y",
) + 
  # Horizontal layout
  coord_flip( 
    # Specify y-axis limit now, this approach ensures boxplot/dist curves
    # continue to plot offscreen, as we're intentionally overflowing some
    ylim = c(0, 17),
    clip = "off"
  ) +
  
  # # Annotation arrow to show data going off edge of plot
  # annotate(
  #   "segment",
  #   x = 1,
  #   xend = 1,
  #   y = 16.9,
  #   yend = 17.9,
  #   arrow = arrow(length = unit(0.4, "cm")),
  #   linewidth = 1,
  # ) +
  # 
  # annotate(
  #   "segment", 
  #    x = 4, 
  #    xend = 4, 
  #   y = 16.9, 
  #   yend = 17.9, 
  #   arrow = arrow(length = unit(0.4, "cm")), 
  #   linewidth = 1,
  #   colour = less_saturated_colors[2]
  #   ) +
  # 
  # annotate(
  #   "segment", 
  #   x = 7, 
  #   xend = 7, 
  #   y = 16.9, 
  #   yend = 17.9, 
  #   arrow = arrow(length = unit(0.4, "cm")), 
  #   linewidth = 1,
  #   colour = less_saturated_colors[3]
  # ) +
  # 
  # annotate(
  #   "segment", 
  #   x = 9.7, 
  #   xend = 9.7, 
  #   y = 16.9, 
  #   yend = 17.9, 
  #   arrow = arrow(length = unit(0.4, "cm")), 
  #   linewidth = 1,
  #   colour = less_saturated_colors[4]
  # ) +
  
  # Override x-axis range and ticks
  scale_y_continuous(
    #limits = c(0, 20),
    #breaks = seq(0, 1000, by = 5)
    # breaks = pretty(range(data_long$duration, na.rm = TRUE), n = 6, min.n = 4),
    #expand = c(0.05, 1)
  ) +
  # Axis Labels
        labs(
          x = NULL,
          y = "Overhead (microseconds)",
          title = "STM32 UART Overhead (transport duration subtracted)",
          subtitle = "F429 Nucleo-144 @ 168MHz, LL + FiFO",
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
          
          strip.text.x = element_text(face = "bold", size = 12, color = "black",hjust = 0),
          strip.placement = "outside",
          panel.spacing = unit(0.5, "lines")

          #axis.line.y = element_line(color = "grey", size = 1) # Adjust line thickness
        )

p 

save_plot("test.svg", fig = p, width=30, height=28)

