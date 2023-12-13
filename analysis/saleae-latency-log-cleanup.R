# Import a set of Saleae Logic 2 export csv files using ISO8061 timestamps
# Calculates the time difference between the trigger (CH0) and the 'done' strobe (CH1)
# Exports a column of durations per file found in the workspace

# I don't know R, so sorry in advance!


# Get list of all CSV files
csv_files <- list.files(pattern="*.csv")

# Initialize an empty data frame to store consolidated results
consolidated_df <- data.frame()

# Loop through each file
for (file_name in csv_files) {
  print(paste("Processing file:", file_name))
  
  # Read CSV file into a data frame with custom column names
  df <- read.csv(file_name, col.names=c("Time", "Channel0", "Channel1"))

  # Handle timestamp formatting i.e. 2023-10-23T23:10:34.036973000+00:00 

  # Extract nanoseconds into another column, reformat time
  nanoseconds <- gsub("^.*\\.(\\d{9}).*$", "\\1", df$Time)
  df$Nanoseconds <- as.numeric(nanoseconds)
  df$Time <- as.POSIXct(sub("\\.\\d{9}", "", df$Time), format="%Y-%m-%dT%H:%M:%S", tz="UTC")

  # Create an empty vector to store Time_Diff for this file
  time_diff_vector <- numeric(0)
  
  # Identify start and end indices
  start_idx <- which(df$Channel0 == 1 & df$Channel1 == 0)
  end_idx <- which((df$Channel0 == 0 | df$Channel0 == 1) & df$Channel1 == 1)
  
  # Calculate time differences
    for (s in start_idx) {
      # Check the row above this pattern was 0,0 i.e. reset state.
      if (s > 1 && df$Channel0[s - 1] == 0 && df$Channel1[s - 1] == 0) {
        
        for (e in end_idx) {
          if (e > s) {
            start_time_s <- as.numeric(df$Time[s])
            start_time_ns <- df$Nanoseconds[s]
            
            end_time_s <- as.numeric(df$Time[e])
            end_time_ns <- df$Nanoseconds[e]
            
            time_diff_s <- end_time_s - start_time_s
            time_diff_ns <- end_time_ns - start_time_ns
            
            if (time_diff_ns < 0) {
              time_diff_ns <- 1e9 + time_diff_ns
              time_diff_s <- time_diff_s - 1
            }
            
            # Combine time differences
            time_diff_sec <- time_diff_s + (time_diff_ns / 1e9)
            
            # Append to this file's Time_Diff vector (in milliseconds)
            time_diff_vector <- c(time_diff_vector, (time_diff_sec*1e3))

            break
          }
        }
      }
    }
  
  # Add this file's Time_Diff vector as a new column to the consolidated data frame
  column_name <- gsub(".csv$", "", file_name)  # Remove .csv from the file name for the column name
  
  if (nrow(consolidated_df) < length(time_diff_vector)) {
    # Extend the data frame with NA values if new vector is longer
    consolidated_df[nrow(consolidated_df) + 1:length(time_diff_vector), ] <- NA
  }
  
  consolidated_df[1:length(time_diff_vector), column_name] <- time_diff_vector
}

# Export 'cleaned' latency duration as csv
write.csv(consolidated_df, "consolidated_df.csv", row.names = FALSE)
