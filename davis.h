/*
 *  davis.h
 *  Mcp
 *
 *  Created by Martin Robinson on 17/08/2007.
 *  Copyright 2007,2008,2009,2010 Naturalwatt Ltd. All rights reserved.
 *  Copyright 2010 Wattsure Ltd. All rights reserved.
 *
 */

// $Id: davis.h,v 3.5 2013/04/08 20:20:55 martin Exp $

/* Davis Vantage objects have defined and fixed lengths */
#define DAVIS
#define	DREALTIMELEN	(97)
#define DHILOWLEN		(436)
#define DGRAPHLEN		(4096)

/* graphic data sets- memory pointer offsets */
#define GRAPH_START              176
/* WARNING - vpweatherpro uses 177, Davis documentation uses 176 */

#define NEXT_10MIN_PTR           GRAPH_START+1
#define NEXT_15MIN_PTR           GRAPH_START+2
#define NEXT_HOUR_PTR            GRAPH_START+3
#define NEXT_DAY_PTR             GRAPH_START+4
#define NEXT_MONTH_PTR           GRAPH_START+5
#define NEXT_YEAR_PTR            GRAPH_START+6
#define NEXT_RAIN_STORM_PTR      GRAPH_START+7
#define NEXT_RAIN_YEAR_PTR       GRAPH_START+8
#define START                    325

#define TEMP_IN_HOUR                 START +    0            /* 24 ||  1 */
#define TEMP_IN_DAY_HIGHS            START +   24            /* 24 ||  1 */
#define TEMP_IN_DAY_HIGH_TIMES       START +   48            /* 24 ||  2 */
#define TEMP_IN_DAY_LOWS             START +   96            /* 24 ||  1 */
#define TEMP_IN_DAY_LOW_TIMES        START +  120            /* 24 ||  2 */
#define TEMP_IN_MONTH_HIGHS          START +  168            /* 25 ||  1 */
#define TEMP_IN_MONTH_LOWS           START +  193            /* 25 ||  1 */
#define TEMP_IN_YEAR_HIGHS           START +  218            /*  1 ||  1 */
#define TEMP_IN_YEAR_LOWS            START +  219            /*  1 ||  1 */

#define TEMP_OUT_HOUR                START +  220            /* 24 ||  1 */
#define TEMP_OUT_DAY_HIGHS           START +  244            /* 24 ||  1 */
#define TEMP_OUT_DAY_HIGH_TIMES      START +  268            /* 24 ||  2 */
#define TEMP_OUT_DAY_LOWS            START +  316            /* 24 ||  1 */
#define TEMP_OUT_DAY_LOW_TIMES       START +  340            /* 24 ||  2 */
#define TEMP_OUT_MONTH_HIGHS         START +  388            /* 25 ||  1 */
#define TEMP_OUT_MONTH_LOWS          START +  413            /* 25 ||  1 */
#define TEMP_OUT_YEAR_HIGHS          START +  438            /* 25 ||  1 */
#define TEMP_OUT_YEAR_LOWS           START +  463            /* 25 ||  1 */

#define DEW_HOUR                     START +  488            /* 24 ||  1 */
#define DEW_DAY_HIGHS                START +  512            /* 24 ||  1 */
#define DEW_DAY_HIGH_TIMES           START +  536            /* 24 ||  2 */
#define DEW_DAY_LOWS                 START +  584            /* 24 ||  1 */
#define DEW_DAY_LOW_TIMES            START +  608            /* 24 ||  2 */
#define DEW_MONTH_HIGHS              START +  656            /* 25 ||  1 */
#define DEW_MONTH_LOWS               START +  681            /* 25 ||  1 */
#define DEW_YEAR_HIGHS               START +  706            /*  1 ||  1 */
#define DEW_YEAR_LOWS                START +  707            /*  1 ||  1 */

#define CHILL_HOUR                   START +  708            /* 24 ||  1 */
#define CHILL_DAY_LOWS               START +  732            /* 24 ||  1 */
#define CHILL_DAY_LOW_TIMES          START +  756            /* 24 ||  2 */
#define CHILL_MONTH_LOWS             START +  804            /* 25 ||  1 */
#define CHILL_YEAR_LOWS              START +  829            /*  1 ||  1 */

#define THSW_HOUR                    START +  830            /* 24 ||  1 */
#define THSW_DAY_HIGHS               START +  854            /* 24 ||  1 */
#define THSW_DAY_HIGH_TIMES          START +  878            /* 24 ||  2 */
#define THSW_MONTH_HIGHS             START +  926            /* 25 ||  1 */
#define THSW_YEAR_HIGHS              START +  951            /*  1 ||  1 */

#define HEAT_HOUR                    START +  952            /* 24 ||  1 */
#define HEAT_DAY_HIGHS               START +  976            /* 24 ||  1 */
#define HEAT_DAY_HIGH_TIMES          START + 1000            /* 24 ||  2 */
#define HEAT_MONTH_HIGHS             START + 1048            /* 25 ||  1 */
#define HEAT_YEAR_HIGHS              START + 1073            /*  1 ||  1 */

#define HUM_IN_HOUR                  START + 1074            /* 24 ||  1 */
#define HUM_IN_DAY_HIGHS             START + 1098            /* 24 ||  1 */
#define HUM_IN_DAY_HIGH_TIMES        START + 1122            /* 24 ||  2 */
#define HUM_IN_DAY_LOWS              START + 1170            /* 24 ||  1 */
#define HUM_IN_DAY_LOW_TIMES         START + 1194            /* 24 ||  2 */
#define HUM_IN_MONTH_HIGHS           START + 1242            /* 25 ||  1 */
#define HUM_IN_MONTH_LOWS            START + 1267            /* 25 ||  1 */
#define HUM_IN_YEAR_HIGHS            START + 1292            /*  1 ||  1 */
#define HUM_IN_YEAR_LOWS             START + 1293            /*  1 ||  1 */

#define HUM_OUT_HOUR                 START + 1294            /* 24 ||  1 */
#define HUM_OUT_DAY_HIGHS            START + 1318            /* 24 ||  1 */
#define HUM_OUT_DAY_HIGH_TIMES       START + 1342            /* 24 ||  2 */
#define HUM_OUT_DAY_LOWS             START + 1390            /* 24 ||  1 */
#define HUM_OUT_DAY_LOW_TIMES        START + 1414            /* 24 ||  2 */
#define HUM_OUT_MONTH_HIGHS          START + 1462            /* 25 ||  1 */
#define HUM_OUT_MONTH_LOWS           START + 1487            /* 25 ||  1 */
#define HUM_OUT_YEAR_HIGHS           START + 1512            /*  1 ||  1 */
#define HUM_OUT_YEAR_LOWS            START + 1513            /*  1 ||  1 */

#define BAR_15_MIN                   START + 1514            /* 24 ||  2 */
#define BAR_HOUR                     START + 1562            /* 24 ||  2 */
#define BAR_DAY_HIGHS                START + 1610            /* 24 ||  2 */
#define BAR_DAY_HIGH_TIMES           START + 1658            /* 24 ||  2 */
#define BAR_DAY_LOWS                 START + 1706            /* 24 ||  2 */
#define BAR_DAY_LOW_TIMES            START + 1754            /* 24 ||  2 */
#define BAR_MONTH_HIGHS              START + 1802            /* 25 ||  2 */
#define BAR_MONTH_LOWS               START + 1852            /* 25 ||  2 */
#define BAR_YEAR_HIGHS               START + 1902            /*  1 ||  2 */
#define BAR_YEAR_LOWS                START + 1904            /*  1 ||  2 */

#define WIND_SPEED_10_MIN_AVG        START + 1906            /* 24 ||  1 */
#define WIND_SPEED_HOUR_AVG          START + 1930            /* 24 ||  1 */
#define WIND_SPEED_HOUR_HIGHS		 START + 1954			 /* 24 ||  1  NEW */
#define WIND_SPEED_DAY_HIGHS         START + 1978            /* 24 ||  1 */
#define WIND_SPEED_DAY_HIGH_TIMES    START + 2002            /* 24 ||  2 */
#define WIND_SPEED_DAY_HIGH_DIR      START + 2050            /* 24 ||  1 */
#define WIND_SPEED_MONTH_HIGHS       START + 2074            /* 25 ||  1 */
#define WIND_SPEED_MONTH_HIGH_DIR    START + 2099            /* 25 ||  1 */
#define WIND_SPEED_YEAR_HIGHS        START + 2124            /* 25 ||  1 */
#define WIND_SPEED_YEAR_HIGH_DIR     START + 2149            /* 25 ||  1 */

#define WIND_DIR_HOUR                START + 2174            /* 24 ||  1 */
#define WIND_DIR_DAY                 START + 2198            /* 24 ||  1 */
#define WIND_DIR_MONTH               START + 2222            /* 24 ||  1 */
#define WIND_DIR_DAY_BINS            START + 2246            /*  8 ||  2 */
#define WIND_DIR_MONTH_BINS          START + 2262            /*  8 ||  2 */

#define RAIN_RATE_1_MIN              START + 2278            /* 24 ||  2 */
#define RAIN_RATE_HOUR               START + 2326            /* 24 ||  2 */
#define RAIN_RATE_DAY_HIGHS          START + 2374            /* 24 ||  2 */
#define RAIN_RATE_DAY_HIGH_TIMES     START + 2422            /* 24 ||  2 */
#define RAIN_RATE_MONTH_HIGHS        START + 2470            /* 25 ||  2 */
#define RAIN_RATE_YEAR_HIGHS         START + 2520            /* 25 ||  2 */

#define RAIN_15_MIN                  START + 2570            /* 24 ||  1 */
#define RAIN_HOUR                    START + 2594            /* 24 ||  2 */
#define RAIN_STORM                   START + 2642            /* 25 ||  2 */
#define RAIN_STORM_START             START + 2692            /* 25 ||  2 */
#define RAIN_STORM_END               START + 2742            /* 25 ||  2 */
#define RAIN_DAY_TOTAL               START + 2792            /* 25 ||  2 */
#define RAIN_MONTH_TOTAL             START + 2842            /* 25 ||  2 */
#define RAIN_YEAR_TOTAL              START + 2892            /* 25 ||  2 */

#define ET_HOUR                      START + 2942            /* 24 ||  1 */
#define ET_DAY_TOTAL                 START + 2966            /* 25 ||  1 */
#define ET_MONTH_TOTAL               START + 2991            /* 25 ||  2 */
#define ET_YEAR_TOTAL                START + 3041            /* 25 ||  2 */

#define SOLAR_HOUR_AVG               START + 3091            /* 24 ||  2 */
#define SOLAR_DAY_HIGHS              START + 3139            /* 24 ||  2 */
#define SOLAR_DAY_HIGH_TIMES         START + 3187            /* 24 ||  2 */
#define SOLAR_MONTH_HIGHS            START + 3235            /* 25 ||  2 */
#define SOLAR_YEAR_HIGHS             START + 3237            /*  1 ||  2 */

#define UV_HOUR_AVG                  START + 3239            /* 24 ||  1 */
#define UV_MEDS_HOUR                 START + 3263            /* 24 ||  1 */
#define UV_MEDS_DAY                  START + 3287            /* 24 ||  1 */
#define UV_DAY_HIGHS                 START + 3311            /* 24 ||  1 */
#define UV_DAY_HIGH_TIMES            START + 3335            /* 24 ||  2 */
#define UV_MONTH_HIGHS               START + 3383            /* 25 ||  1 */
#define UV_YEAR_HIGHS                START + 3384            /*  1 ||  1 */

#define LEAF_HOUR                    START + 3385            /* 24 ||  1 */
#define LEAF_DAY_LOWS                START + 3409            /* 24 ||  1 */
#define LEAF_DAY_LOW_TIMES           START + 3433            /* 24 ||  2 */
#define LEAF_DAY_HIGHS               START + 3481            /* 24 ||  1 */
#define LEAF_DAY_HIGH_TIMES          START + 3505            /* 24 ||  2 */
// #define WIND_SPEED_HOUR_HIGHS        START + 3553            // 24 ||  1  DELETED */
#define LEAF_MONTH_LOWS              START + 3553            /*  1 ||  1 */
#define LEAF_MONTH_HIGHS             START + 3554            /*  1 ||  1 */
#define LEAF_YEAR_LOWS               START + 3555            /*  1 ||  1 */
#define LEAF_YEAR_HIGHS              START + 3556            /*  1 ||  1 */

#define SOIL_HOUR                    START + 3557            /* 24 ||  1 */
#define SOIL_DAY_LOWS                START + 3581            /* 24 ||  1 */
#define SOIL_DAY_LOW_TIMES           START + 3605            /* 24 ||  2 */
#define SOIL_DAY_HIGHS               START + 3653            /* 24 ||  1 */
#define SOIL_DAY_HIGH_TIMES          START + 3677            /* 24 ||  2 */
#define SOIL_MONTH_LOWS              START + 3725            /*  1 ||  1 */
#define SOIL_MONTH_HIGHS             START + 3726            /*  1 ||  1 */
#define SOIL_YEAR_LOWS               START + 3727            /*  1 ||  1 */
#define SOIL_YEAR_HIGHS              START + 3728            /*  1 ||  1 */
#define SOIL_YEAR_HIGHS_COMP         START + 3729            /*  1 ||  1 */

#define RX_PERCENTAGE                START + 3730            /*  24 ||  1 */

#define SAVE_MIN                     RX_PERCENTAGE+25 /* = 4084 */
#define SAVE_HOUR                    SAVE_MIN+1 */
#define SAVE_DAY                     SAVE_HOUR+1 */
#define SAVE_MONTH                   SAVE_HOUR+2 */
#define SAVE_YEAR                    SAVE_HOUR+3 */
#define SAVE_YEAR_COMP               SAVE_HOUR+4 */
#define BAUD_RATE                    SAVE_HOUR+5 */
#define DEFAULT_RATE_GRAPH           SAVE_HOUR+6 */
#define LCD_MODEL					 SAVE_HOUR+8 */
#define LCD_MODEL_COMP				 SAVE_HOUR+9 */
#define LOG_AVERAGE_TEMPS		     SAVE_HOUR+11	 /* Verify this is 4092 */
