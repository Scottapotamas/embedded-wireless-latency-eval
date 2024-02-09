library(ggplot2)
library(ggtext)
library(ggridges)
library(ggdist)
library(dplyr)
library(sjPlot)
library(readr)
library(tidyr)

# Read the CSV file into a data frame
data <- read.csv("module-comparison-data.csv", 
                 check.names = FALSE, 
                 stringsAsFactors = FALSE)

# Reshape from wide to long format
data_long <- data %>%
  pivot_longer(
    cols = everything(), 
    names_to = "category", 
    values_to = "duration"
  ) %>%
  # Create the packetsize column by extracting it from the column name
  mutate(packetsize = gsub("-.*", "", category),
         # Create the subgroup column (12B, 128B, 1024B)
         subgroup = gsub(".*-", "", category)) # %>%

# Compute stats
summary_data <- data_long %>%
  filter(!is.na(duration)) %>%
  group_by(subgroup, packetsize) %>%
  summarise(
    median = median(duration),
    mean = mean(duration),
    lower_quantile = quantile(duration, 0.25),
    upper_quantile = quantile(duration, 0.75),
    min = min(duration),
    max = max(duration),
    variance = var(duration),
    num_samples = length(duration)
  )%>%
  ungroup() %>%
  arrange(upper_quantile)

print(summary_data)

# Reorder based on the statistic and packetsize filter
reorder_variable <- summary_data %>%
  filter(packetsize == '12B') %>%
  arrange(-upper_quantile) %>%
  pull(subgroup)
# Apply the new order
summary_data$subgroup <- factor(summary_data$subgroup, levels = reorder_variable)

# Custom bar colours
myPalette <- c("12B"="#004c6d", "128B"="#7295b0", "1024B"="#d0e5f8")

# Plotting
p <- summary_data %>% 
  ggplot(aes(x = subgroup)) +

  # Manually draw bars in correct draw order...
  # 1024B bars
  geom_col(data = subset(summary_data, packetsize == "1024B"), 
             aes(y = upper_quantile, fill = packetsize), 
             position = "identity", 
             alpha = 0.8,
             width = .6) +
  # 128B bars
  geom_col(data = subset(summary_data, packetsize == "128B"), 
                  aes(y = upper_quantile, fill = packetsize), 
                  position = "identity", 
                  alpha = 0.8,
                  width = .6) +
  # 12B bars
  geom_col(data = subset(summary_data, packetsize == "12B"),
                  aes(y = upper_quantile, fill = packetsize),
                  position = "identity",
                  alpha = 0.8, 
                  width = .6) +
  # Label values under each bar
  geom_text(
    aes(y = upper_quantile, label = ifelse(upper_quantile > 20, as.character(signif(upper_quantile, 3)), as.character(signif(upper_quantile, 2)))),
    family = "Roboto Mono",
    size = 3.6,
    vjust = 3.15,
    alpha = 0.8,
    check_overlap = TRUE, # prevent overdraw
  ) +
  # When a value clips offscreen, render it on-canvas as a text label and arrow
  geom_text( aes(y = upper_quantile, 
                 label = ifelse(upper_quantile > 150, as.character(paste(round(upper_quantile, 0), "→")), '')
                 ), 
             y = 149,
             vjust = 3.15,
             alpha = 0.8,
             size = 3.6,
             family = "Roboto Mono",
             fontface = "bold",
             
             ) +
  # Manual Legend formatting
  scale_fill_manual(
    values = myPalette,
    name = "Payload Size",
    breaks = c("12B", "128B", "1024B"),
  ) +
  annotate("text", 
           label = "*Sorted by 12B result",
           size = 4,
           alpha = 0.7,
           x = 9.7, 
           y = 130, 
           ) +
  annotate("text", 
           label = "*Tested indoors, 1m range",
           size = 4,
           alpha = 0.7,
           x = 9.3, 
           y = 132.5, 
  ) +
  # Horizontal layout
  coord_flip(
    ylim = c(0, 146),
    # clip = "on"
  ) +
  # Override x-axis range and ticks
  scale_y_continuous(
    limits = c(0, NA),
    breaks = seq(0, 1000, by = 10)
  ) +
  # Axis Labels
    labs(
      x = NULL,
      y = "Duration (milliseconds)",
      title = "Wireless Latency Benchmark Results",
      subtitle = "75% of payload transfers complete faster than...",
      caption = "Lower is better."
    ) +
    theme_minimal() +
    theme(
      text = element_text(family = "Roboto Mono", size = 20, face = "plain"),

      # Legend Formatting
      legend.position = c(0.85, 0.87),
      legend.title = element_text( 
        size=15, 
        face="bold"
       ),
      legend.spacing.x = unit(0.5, 'cm'),
      legend.background = element_rect(fill="white", 
                                       size=0.0, linetype="solid"),
      
      plot.margin = margin(5, 5, 5, 5),
      plot.title = element_text(hjust = 0.5),    # Center the title
      plot.subtitle = element_text(hjust = 0.5, size = 16), # Center the subtitle
      plot.caption = element_text(hjust = 0.5),   # Center the caption
      axis.text.x = element_text(margin = margin(t = 8, r = 0, b = 15, l = 0)), # +Gap between axis and label

      # axis.line.y = element_line(color = "grey", size = 1) # Adjust line thickness
    )

save_plot("test.svg", fig = p, width=30, height=25)

