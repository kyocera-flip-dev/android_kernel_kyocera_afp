/*
 * This software is contributed or developed by KYOCERA Corporation.
 * (C) 2014 KYOCERA Corporation
 * (C) 2015 KYOCERA Corporation
 * (C) 2016 KYOCERA Corporation
 */
/*
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 * 
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

#include "sensor_driver.h"
#include "sensor_micon_driver.h"
#include "sensor_com.h"

#define DEFAULT_ORTN_DELAY    (30)

static ssize_t ortn_batch_store(struct device *dev,
    struct device_attribute *attr,
    const char *buf,
    size_t count);
static ssize_t ortn_batch_data_show(struct device *dev,
    struct device_attribute *attr,
    char *buf);
static ssize_t ortn_flush_store(struct device *dev,
    struct device_attribute *attr,
    const char *buf,
    size_t count);
static ssize_t ortn_enable_show(struct device *dev,
    struct device_attribute *attr,
    char *buf );
static ssize_t ortn_enable_store(struct device *dev,
    struct device_attribute *attr,
    const char *buf,
    size_t count );
static ssize_t ortn_delay_show(struct device *dev,
    struct device_attribute *attr,
    char *buf);
static ssize_t ortn_delay_store(struct device *dev,
    struct device_attribute *attr,
    const char *buf,
    size_t count);
static ssize_t ortn_data_show(
    struct device *dev,
    struct device_attribute *attr,
    char *buf);
static ssize_t ortn_status_show(
    struct device *dev,
    struct device_attribute *attr,
    char *buf );
static ssize_t ortn_status_store(
    struct device *dev,
    struct device_attribute *attr,
    const char *buf,
    size_t count );

static void ortn_update_last_read_data(void);
static void ortn_poll_work_func(struct work_struct *work);
static void ortn_set_input_params( struct input_dev *dev );


static DEVICE_ATTR(enable,
    S_IRUSR|S_IWUSR|S_IRGRP|S_IWGRP,
    ortn_enable_show,
    ortn_enable_store
);
static DEVICE_ATTR(delay,
    S_IRUSR|S_IWUSR|S_IRGRP|S_IWGRP,
    ortn_delay_show,
    ortn_delay_store
);
static DEVICE_ATTR(data,
    S_IRUSR|S_IRGRP,
    ortn_data_show,
    NULL
);
static DEVICE_ATTR(status,
    S_IRUSR|S_IWUSR|S_IRGRP|S_IWGRP,
    ortn_status_show,
    ortn_status_store
);
static DEVICE_ATTR(batch,
    S_IWUSR|S_IWGRP,
    NULL,
    ortn_batch_store
);
static DEVICE_ATTR(batch_data,
    S_IRUSR|S_IRGRP,
    ortn_batch_data_show,
    NULL
);
static DEVICE_ATTR(flush,
    S_IWUSR|S_IWGRP,
    NULL,
    ortn_flush_store
);

static struct attribute *ortn_attributes[] = {
    &dev_attr_enable.attr,
    &dev_attr_delay.attr,
    &dev_attr_data.attr,
    &dev_attr_status.attr,
    &dev_attr_batch.attr,
    &dev_attr_batch_data.attr,
    &dev_attr_flush.attr,
    NULL
};

static struct attribute_group ortn_attr_grp = {
    .attrs = ortn_attributes
};

struct sensor_input_info_str ortn_input_info =
{
    NULL,
    ortn_set_input_params,
    &ortn_attr_grp,
};

struct sensor_poll_info_str ortn_poll_info = {
    .name       = "ortn_poll_wq",
    .poll_time  = ATOMIC_INIT(DEFAULT_ORTN_DELAY),
    .poll_func  = ortn_poll_work_func,
};

static struct orientation ortn_last_read_data = {
    0,0,0
};

static struct sensor_batch_data_str ortn_batch_data;
static uint32_t g_time_stamp_ori       = 0;
static uint32_t g_input_num_ori        = 0;
extern struct mutex sensor_batch_mutex;

static ssize_t ortn_batch_store(struct device *dev,
    struct device_attribute *attr,
    const char *buf,
    size_t count)
{
    uint32_t ret = 0;
    struct sensor_batch_info_str batch_info;

    SENSOR_N_LOG("start");

    sscanf( buf, "%d %d %d",
             &batch_info.flags,
             &batch_info.period_ns,
             &batch_info.timeout );
    SENSOR_N_LOG("parm - flags[%x] period_ns[%x] timeout[%x]",
                  (int)batch_info.flags,
                  (int)batch_info.period_ns,
                  (int)batch_info.timeout );

    ret = sensor_set_batch( SENSOR_ORTN, batch_info);

    SENSOR_N_LOG("end - return[%d]",count);
    return count;
}

static ssize_t ortn_batch_data_show(struct device *dev,
    struct device_attribute *attr,
    char *buf)
{
    ssize_t ret =0;

    SENSOR_N_LOG("start");

    mutex_lock(&sensor_batch_mutex);

    ret = sprintf(buf, "%d %d %d %d\n",ortn_batch_data.payload_size,
                       ortn_batch_data.recode_num, g_input_num_ori, g_time_stamp_ori);

    g_time_stamp_ori = 0;

    mutex_unlock(&sensor_batch_mutex);

    SENSOR_N_LOG("end");

    return ret;
}

void ortn_ring_buffer_timestamp(
    uint32_t time_stamp_ori)
{
    SENSOR_N_LOG("start");
    g_time_stamp_ori = time_stamp_ori;
    SENSOR_N_LOG("end %d",g_time_stamp_ori);
}

void ortn_report_batch(
    enum sensor_batch_report_e_type repo_type,
    struct sensor_batch_data_str batch_data )
{
    
    SENSOR_N_LOG("start");

    ortn_batch_data = batch_data;

    sensor_report_batch( ortn_input_info.dev,
                         repo_type,
                         ortn_batch_data );

    SENSOR_N_LOG("end");
    return;
}

void ortn_timestamp_report(void)
{
    SENSOR_N_LOG("start");

    sensor_report_batch( ortn_input_info.dev,
                         SENSOR_COMP_TIME,
                         ortn_batch_data );

    SENSOR_N_LOG("end");
    return;
}

static ssize_t ortn_flush_store(struct device *dev,
    struct device_attribute *attr,
    const char *buf,
    size_t count)
{
    uint32_t ret = 0;

    SENSOR_N_LOG("start");

    ret = sensor_set_flush(SENSOR_ORTN, ortn_input_info.dev);

    SENSOR_N_LOG("end - return[%d]",count);
    return count;
}

static ssize_t ortn_enable_show(struct device *dev,
    struct device_attribute *attr,
    char *buf )
{
    int enable = 0;
    SENSOR_N_LOG("start");

    enable = sensor_get_status(SENSOR_ORTN);

    SENSOR_N_LOG("end ->enable[%d]",enable);
    return sprintf(buf, "%d\n", enable);
}

static ssize_t ortn_enable_store(struct device *dev,
    struct device_attribute *attr,
    const char *buf,
    size_t count )
{
    unsigned long enable = 0;
    int ret = 0;

    SENSOR_N_LOG("start");

    ret = kstrtoul(buf, 10, &enable);
    SENSOR_N_LOG("kstrtoul() ret[%d]->enable[%d]",ret, (int)enable);

    sensor_enable( SENSOR_ORTN, &ortn_poll_info, (bool)enable );

    SENSOR_N_LOG("end - return[%d]",count);
    return count;
}

static ssize_t ortn_delay_show(struct device *dev,
    struct device_attribute *attr,
    char *buf)
{
    int32_t delay = 0;
    SENSOR_N_LOG("start");
    delay = atomic_read(&(ortn_poll_info.poll_time));
    SENSOR_N_LOG("end ->delay[%d]",(int)delay);
    return sprintf(buf, "%d\n", delay);
}

static ssize_t ortn_delay_store(struct device *dev,
    struct device_attribute *attr,
    const char *buf,
    size_t count)
{
    unsigned long delay = 0;
    int ret = 0;
    SENSOR_N_LOG("start");

    ret = kstrtoul(buf, 10, &delay);
    SENSOR_N_LOG("kstrtoul() ret[%d]->delay[%d]",ret, (int)delay);

    sensor_set_poll_time( SENSOR_ORTN, &ortn_poll_info, (int32_t)delay);

    SENSOR_N_LOG("end - return[%d]",count);
    return count;
}

static ssize_t ortn_status_show(
    struct device *dev,
    struct device_attribute *attr,
    char *buf )
{
    int rt = 0;
    SENSOR_N_LOG("start");

    rt = sensor_get_status(SENSOR_ORTN);

    SENSOR_N_LOG("end");
    return sprintf(buf, "%d\n", rt);
}

static ssize_t ortn_status_store(
    struct device *dev,
    struct device_attribute *attr,
    const char *buf,
    size_t count )
{
    int ret = 0;
    unsigned long status = 0;
    SENSOR_N_LOG("start");

    ret = kstrtoul(buf, 10, &status);
    SENSOR_N_LOG("kstrtoul() ret[%d]->status[%d]",ret, (int)status);

    sensor_set_status(SENSOR_ORTN, status);

    SENSOR_N_LOG("end");
    return count;
}

static ssize_t ortn_data_show(
    struct device *dev,
    struct device_attribute *attr,
    char *buf)
{
    ssize_t ret;
    SENSOR_N_LOG("start");
    ortn_update_last_read_data();

    ret = sprintf( buf, "%d %d %d\n",
                   ortn_last_read_data.yaw,
                   ortn_last_read_data.pitch,
                   ortn_last_read_data.roll );

    SENSOR_N_LOG("end");
    return ret;
}

static void ortn_update_last_read_data(void)
{
    unsigned long enable = 0;
    int32_t ret = 0;
    union sensor_read_data_u read_data;

    enable = sensor_get_status(SENSOR_ORTN);

    if (enable) {
        ret = sensor_type_get_data(SENSOR_ORTN, &read_data);
        if (0 == ret) {
            memcpy(&ortn_last_read_data,&(read_data.ortn_data),sizeof(read_data.ortn_data));
        }
    }
}

static void ortn_poll_work_func(struct work_struct *work)
{
    int32_t polltime = 0;

    SENSOR_N_LOG("start");

    if(sns_get_reset_status() == false){
        sns_iio_report_event_now(SENSOR_ORTN);
    }

    polltime = atomic_read(&(ortn_poll_info.poll_time));
    if (polltime > 0) {
        queue_delayed_work( (ortn_poll_info.poll_wq),
                            &(ortn_poll_info.poll_work),
                            msecs_to_jiffies(polltime) );
        SENSOR_A_LOG("start delay work :polltime[%d]",
                     atomic_read(&(ortn_poll_info.poll_time)) );
    } else {
        SENSOR_ERR_LOG("fail polltime[%d]",(int)polltime);
    }
    SENSOR_N_LOG("end");
    return;
}

static void ortn_set_input_params( struct input_dev *dev )
{
    SENSOR_N_LOG("start");

    if(!dev){
        SENSOR_ERR_LOG("bad parm --> dev is NULL");
        return;
    }

    dev->name = "orientation";
    dev->id.bustype = BUS_SPI;

    set_bit(EV_ABS, dev->evbit);
    input_set_abs_params(dev, ABS_RUDDER, INT_MIN, INT_MAX, 0, 0);
    input_set_abs_params(dev, ABS_STATUS, 0, 3, 0, 0);
    input_set_abs_params(dev, ABS_X, INT_MIN, INT_MAX, 0, 0);
    input_set_abs_params(dev, ABS_Y, INT_MIN, INT_MAX, 0, 0);
    input_set_abs_params(dev, ABS_Z, INT_MIN, INT_MAX, 0, 0);
    input_set_abs_params(dev, ABS_MISC, INT_MIN, INT_MAX, 0, 0);

    SENSOR_N_LOG("end");
    return;
}

void ortn_driver_init( void )
{
    int ret = 0;

    SENSOR_N_LOG("start");

    ret = sensor_input_init( &ortn_input_info );
    SENSOR_N_LOG("sensor_input_init()-->ret[%d] dev[%d]",
                  ret, (int)(ortn_input_info.dev) );

    if( (0 != ret) || (NULL == (ortn_input_info.dev))) {
        SENSOR_ERR_LOG("fail sensor_input_init()");
        SENSOR_ERR_LOG("end return[%d]",-ENODEV);
        return;
    }

    sensor_poll_init(SENSOR_ORTN,&ortn_poll_info);

    SENSOR_N_LOG("end");
    return;
}

EXPORT_SYMBOL(ortn_driver_init);

