library(ggplot2)
library(ggtext)
library(ggridges)
library(ggdist)
library(dplyr)
library(sjPlot)
library(readr)
library(tidyr)

# Read the CSV file into a data frame
data <- read.csv("esp32-comparisons.csv", 
                 check.names = FALSE, 
                 stringsAsFactors = FALSE)

# What packet size we're filtering
packet_size_str = '1024B'

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
  filter(!is.na(duration)) %>%
  filter(packetsize == packet_size_str)

# Group data by category and calculate the CDF for each group
data_long <- data_long %>% 
  group_by(category) %>% 
  arrange(duration) %>%
  mutate(cdf = cumsum(duration)/sum(duration))

data_long <- data_long %>%
mutate(subgroup = factor(subgroup, levels = c("ESPNOW", "TCP", "UDP", "Websockets", "SPP", "Bluedroid", "NimBLE" )))
protocol_colours <- c("ESPNOW"="#BBBBBB", "TCP"="#0077BB", "UDP"="#33BBEE", "Websockets"="#009988", "SPP"="#EE3377", "Bluedroid"="#CC3311", "NimBLE"="#EE7733")

p <- data_long  %>% 
  # mutate(packetsize = factor(packetsize, levels = c("12B", "128B", "1024B"))) %>%
  ggplot( aes(x=duration, y=cdf, color=subgroup)) +
  geom_line(
    size = 1.25,
  ) +
  # facet_grid(
  #   packetsize ~ .,
  #   scales = "fixed",
  #   space = "fixed",
  #   switch = "y",
  # ) +
  # Override axis range and ticks
  coord_cartesian( 
    xlim = c(0, 75),
    ) +
  # scale_alpha_manual(
  #   name = "Packet Size",
  #   values = c("1024B" = 0.4, "128B" = 1)
  # ) +
  scale_color_manual(
    name = "Protocol",
    values = protocol_colours,
  ) +
  scale_y_continuous(
    breaks = seq(0, 1, by = 0.25),
    labels = scales::percent
  ) +
  scale_x_continuous(
    limits = c(0, NA),
    breaks = seq(0, 1000, by = 5),
    expand = c(0.01, 1)
  ) +
  # Axis Labels
  labs(
    title = "ESP32 Protocol Latency Comparisons",
    subtitle = paste(packet_size_str, "Packets, IDF 5.1, No PowerSave, IRAM Optimisations"),
    y = "Cumulative Distribution of Results",
    x = "Duration (milliseconds)",
    caption = "Lower is better"
  ) +
  # Styling
  theme_minimal() +
  theme(
    text = element_text(family = "Roboto Mono", size = 20, face = "plain"),
    # Legend Formatting
    legend.position = c(0.87, 0.33),
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
    axis.text.x = element_text(margin = margin(t = 5, r = 0, b = 15, l = 0)), # +Gap between axis and label
    # axis.text.y = element_text(face = "bold"),
    strip.placement = "outside",
    )

save_plot("test.svg", fig = p, width=30, height=16)