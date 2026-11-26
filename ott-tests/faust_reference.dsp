import("stdfaust.lib");
depth = hslider("Depth", 1, 0, 1, 0.01);
time = hslider("Time", 1, 0, 10, 0.01);
low = hslider("Low", 0, -12, 12, 0.1);
mid = hslider("Mid", 0, -12, 12, 0.1);
high = hslider("High", 0, -12, 12, 0.1);
up = hslider("Upward", 1, 0, 2, 0.01);
down = hslider("Downward", 1, 0, 2, 0.01);
process = co.xfer_ott(depth,time,0,-6*depth,up,down,1,1,1,low,mid,high,0,0,0,0,0,0);
