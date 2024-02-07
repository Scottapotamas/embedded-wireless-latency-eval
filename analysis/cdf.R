library(ggplot2)
library(ggtext)
library(ggridges)
library(ggdist)
library(dplyr)
library(sjPlot)
library(readr)
library(tidyr)

# Read the CSV file into a data frame
data <- read.csv("2G4-module-comparison-data.csv", 
                 check.names = FALSE, 
                 stringsAsFactors = FALSE)

# What packet size we're looking at
packet_size_str = '12B'

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
         subgroup = gsub(".*-", "", category)) %>%
  filter(!is.na(duration))%>%
  filter(packetsize == packet_size_str)

# Group data by category and calculate the CDF for each group
data_long <- data_long %>% 
  group_by(category) %>% 
  arrange(duration) %>%
  mutate(cdf = cumsum(duration)/sum(duration))

p <- data_long  %>% 
  ggplot( aes(x=duration, y=cdf, color=category)) +
  geom_line(
    size = 1.25,
  ) +
  # geom_vline(data=ddata.m,
  #            aes(xintercept = value,
  #                color=variable),
  #            linetype = 2,
  #            size=2) +
  
  # Override axis range and ticks
  coord_cartesian( 
    xlim = c(0, 120),
    # clip = "off"
    ) +
  scale_y_continuous(
    breaks = seq(0, 1, by = 0.25)
  ) +
  scale_x_continuous(
    limits = c(0, NA),
    breaks = seq(0, 1000, by = 5)
  ) +
  # scale_x_log10(
  #     limits = c(0.1, 200),
  #     # breaks = seq(0, 1000, by = 10)
  # ) +
  # Axis Labels
  labs(
    title = "2.4 GHz Minimum Latency",
    subtitle = "Some comment",
    y = "Cumulative Distribution Function (CDF)",
    x = "Duration (milliseconds)",
    caption = "Lower is better"
  ) +
  # Styling
  theme_minimal() +
  theme(
    text = element_text(family = "Roboto Mono", size = 20, face = "plain"),
    legend.position = "top",
    # legend.justification = "left",
    # legend.box.margin = margin(6, 6, 6, 6),
    # legend.spacing.x = unit(0.1, 'cm'),
    # legend.spacing.y = unit(0, 'cm'),
    
    # plot.margin = margin(5, 5, 5, 5),
    plot.title = element_text(hjust = 0.5),    # Center the title
    plot.subtitle = element_text(hjust = 0.5, size = 16), # Center the subtitle
    plot.caption = element_text(hjust = 0.5),   # Center the caption
    axis.text.x = element_text(margin = margin(t = 5, r = 0, b = 15, l = 0)), # +Gap between axis and label
    # axis.text.y = element_text(face = "bold"),
    
    # strip.text.x = element_text(face = "bold", size = 12, color = "black",hjust = 0),
    # strip.placement = "outside",
    # panel.spacing = unit(1, "lines"),
    )

save_plot("test.svg", fig = p, width=30, height=24)


# myPalette <- c("Minimum"="#004c6d", "Lower Quartile"="#7295b0", "Median"="#d0e5f8")

# Plotting
o <- summary_data %>% 
  # Re-order based on some chosen statistic
  arrange(packetsize, min) %>%
  mutate(subgroup = factor(subgroup, 
                           levels = unique(subgroup))) %>%
  # Bar-chart
  ggplot(aes(x = packetsize)) +
  # Facets on 12B, 128B, 1024B
  facet_grid(rows = vars(subgroup),
             scales = "free",
             space = "fixed",
             switch = "y",             
  ) +
  # Bar Charts, draw order is back to front
  # geom_bar( aes(y = upper_quartile), 
  #           stat="identity", 
  #           alpha = 0.10,
  #           width = .75, 
  # ) +
  geom_bar( aes(y = median, fill = "Median"),
            stat="identity",
            # fill = "#0066CC",
            alpha = 1,
            width = .75,
  ) +
  geom_bar( aes(y = lower_quartile, fill = "Lower Quartile"), 
            stat="identity", 
            alpha = 1,
            width = .75, 
  ) +
  geom_bar( aes(y = min, fill = "Minimum"), 
            stat="identity", 
            alpha = 1,
            width = .75, 
  ) +
  scale_fill_manual(
    values = myPalette,
    name = "",
    breaks = c("Minimum", "Lower Quartile", "Median"),
  ) +
  # Horizontal layout
  coord_flip(
    ylim = c(0, 50),
    # clip = "off"
  ) +
  # Override x-axis range and ticks
  scale_y_continuous(
    limits = c(0, NA),
    breaks = seq(0, 1000, by = 10)
    # breaks = pretty(range(summary_data$median, na.rm = TRUE), n = 12, min.n = 4),
    #expand = c(0.05, 1)
  ) +
  # Axis Labels
  labs(
    x = NULL,
    y = "Duration (milliseconds)",
    # title = paste(packet_size_str, "Transfer Duration Comparisons"),
    title = paste("2.4 GHz Minimum Latency"),
    subtitle = "Some comment",
    caption = "Lower is better"
  ) +
  theme_minimal() +
  theme(
    text = element_text(family = "Roboto Mono", size = 20, face = "plain"),
    # legend.position = "none",
    legend.position = "top",
    # legend.justification = "left",
    # legend.box.margin = margin(6, 6, 6, 6),
    # legend.spacing.x = unit(0.1, 'cm'),
    # legend.spacing.y = unit(0, 'cm'),
    
    plot.margin = margin(5, 5, 5, 5),
    plot.title = element_text(hjust = 0.5),    # Center the title
    plot.subtitle = element_text(hjust = 0.5, size = 16), # Center the subtitle
    plot.caption = element_text(hjust = 0.5),   # Center the caption
    axis.text.x = element_text(margin = margin(t = 5, r = 0, b = 15, l = 0)), # +Gap between axis and label
    # axis.text.y = element_text(face = "bold"),
    
    # strip.text.x = element_text(face = "bold", size = 12, color = "black",hjust = 0),
    strip.placement = "outside",
    panel.spacing = unit(1, "lines"),
    
    # axis.line.y = element_line(color = "grey", size = 1) # Adjust line thickness
  )

# save_plot("test.svg", fig = p, width=30, height=38)




