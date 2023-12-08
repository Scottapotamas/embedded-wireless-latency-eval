library(ggplot2)
library(ggtext)
library(ggridges)
library(ggdist)
library(dplyr)
library(sjPlot)

# Sum the number of samples to show as n=123 text
add_sample <- function(x){
  return(c(y = max(x) + .025, 
           label = length(x)))
}

# Main Chart
p <- diamonds %>% 
  filter(!is.na(price)) %>%
  ggplot(aes(x = price, y = cut)) + 
  # Distribution curve
  ggdist::stat_halfeye(
    aes(),
    adjust = .5, 
    scale = 0.75,
    height = 0.75,
    width = .75, 
    .width = 0,
    justification = -.4, 
    point_color = NA) + 
  #Boxplot
  geom_boxplot(
    aes(),
    position=position_dodge(5),
    width = .3, 
    outlier.shape = NA
  ) +
  # Median value text
  stat_summary(
    geom = "text",
    fun = "median",
    aes(label = round(..x.., 2)),
    family = "Roboto Mono",
    fontface = "bold",
    size = 4,
    vjust = 3
  ) +
  # Sample Count text
  stat_summary(
    geom = "text",
    fun.data = add_sample,
    aes(label = paste("n =", ..label..)),
    family = "Roboto Condensed",
    size = 4,
    hjust = 1
  ) +
  # Axis Labels
        labs(
          x = "Duration (milliseconds)",
          y = NULL,
          title = "Meaningful title goes here",
          subtitle = "A subtitle for context",
          caption = "Smaller is better"
        ) +
        theme_minimal(base_family = "Roboto Mono", base_size = 15) +
        theme(
          legend.position = "none",
          plot.margin = margin(15, 15, 10, 15),
          plot.title = element_text(hjust = 0.5),    # Center the title
          plot.subtitle = element_text(hjust = 0.5), # Center the subtitle
          plot.caption = element_text(hjust = 0.5),   # Center the caption
          #panel.grid.minor = element_blank(),
          #panel.grid.major.y = element_blank(),
          #axis.ticks = element_blank(),
          #axis.text.x = element_text(family = "Roboto Mono"),

        )

p

save_plot("test.svg", fig = p, width=30, height=15)

