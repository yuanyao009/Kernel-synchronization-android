#include<linux/syscalls.h>
#include<linux/uaccess.h>
#include<linux/kernel.h>
#include<linux/idr.h>
#include<linux/errno.h>
#include<linux/slab.h>
#include<linux/kfifo.h>
#include<asm/atomic.h>

static DEFINE_KFIFO(dev_acc_list, struct dev_acceleration, 32);
static DEFINE_SPINLOCK(dev_acc_lock);
static DEFINE_IDR(event_idr);
static DEFINE_SPINLOCK(event_lock);

struct acc_event {
	int event_is_valid;
	struct acc_motion acc_m;
    
	atomic_t event_count;
	wait_queue_head_t ev_waitq;
};

static void get_event(struct acc_event *event)
{
	atomic_inc(&event->event_count);
}

static void put_event(struct acc_event *event)
{
	if (atomic_dec_and_test(&event->event_count))
		kfree(event);
}

static inline int check(struct acc_event *ev)
{
	int window_event(struct acc_event *event, struct kfifo *kfifo);
	return 0;
}

static int insert_ev(struct acc_event *ev)
{
	int er;
	int id = -1;
    
	if (unlikely(!idr_pre_get(&event_idr, GFP_KERNEL)))
		return -ENOMEM;
	spin_lock(&event_lock);
	er = idr_get_new_above(&event_idr, ev, 1, &id);
	if (!er)
		ev->event_is_valid = 1;
	spin_unlock(&event_lock);
	if (er)
		return er;
	return id;
}

int window_event(struct acc_event *event, struct kfifo *kfifo)
{
	int i = 0;
	int fx = 0, fy = 0, fz = 0;
	int si;
	struct dev_acceleration dev_acc0;
	struct dev_acceleration dev_acc1;
	struct acc_event *ev = event;
    
	pr_info(" spin");
	spin_lock(&dev_acc_lock);
	if (kfifo_avail(&dev_acc_list)) {
		si = kfifo_out(&dev_acc_list, &dev_acc0, sizeof(dev_acc0));
		if (si != sizeof(dev_acc0))
			return -EINVAL;
	}
	while (kfifo_avail(&dev_acc_list)) {
		pr_info("kfifo");
		si = kfifo_out(&dev_acc_list, &dev_acc1, sizeof(dev_acc1));
		if (si != sizeof(dev_acc1))
			return -EINVAL;
		fx = abs(dev_acc1.x - dev_acc0.x);
		pr_info("fx is %d", fx);
		fy = abs(dev_acc1.y - dev_acc0.y);
		pr_info("fy is %d", fy);
		fz = abs(dev_acc1.z - dev_acc0.z);
		pr_info("fz is %d", fz);
		if ((fx + fy + fz) > NOISE) {
			if (fx > ev->acc_m.dlt_x || fy > ev->acc_m.dlt_y || fz > ev->acc_m.dlt_z)
				i++;
		}
		dev_acc0 = dev_acc1;
	}
	if (i > ev->acc_m.frq)
		pr_info("return");
    return 1;
	return 0;
}

SYSCALL_DEFINE1(set_acceleration, struct dev_acceleration *, ac)
{
	struct dev_acceleration ker_acc;
    
	pr_info("Acceleration measurement:\n");
	if (current_uid() != 0)
		return -EACCES;
	if (ac == NULL)
		return -EINVAL;
	if (copy_from_user(&ker_acc, ac, sizeof(ker_acc)))
		return -EFAULT;
	pr_info(" x=%d, y=%d, z=%d\n", ker_acc.x, ker_acc.y, ker_acc.z);
	kfree(&ker_acc);
	return 0;
}

SYSCALL_DEFINE1(accevt_signal, struct dev_acceleration __user *, ac)
{
	struct dev_acceleration ker_acc;
	struct acc_event *ev;
	int id, n = 0;
    
	if (current_uid() != 0)
		return -EACCES;
	if (ac == NULL)
		return -EINVAL;
	if (copy_from_user(&ker_acc, ac, sizeof(ker_acc)))
		return -EFAULT;
	if (kfifo_is_full(&dev_acc_list))
		return -EFAULT;
	spin_lock(&dev_acc_lock);
	if (!kfifo_in(&dev_acc_list, &ker_acc, sizeof(struct dev_acceleration)))
		return -EFAULT;
	spin_unlock(&dev_acc_lock);
	id = 0;
	if (WINDOW == kfifo_size(&dev_acc_list)) {
		while (1) {
			spin_lock(&event_lock);
			ev = idr_get_next(&event_idr, &id);
			spin_unlock(&event_lock);
			if (!ev)
				break;
			if (window_event(ev, &dev_acc_list)) {
				wake_up(&ev->ev_waitq);
				i = 1;
				n = 1;
				kfifo_reset(&dev_acc_list);
			}
			id++;
		}
	}
	return n;
}

SYSCALL_DEFINE1(accevt_create, struct acc_motion __user *, acc_motion)
{
	struct acc_motion kacc_motion;
	struct acc_event *ev;
	int id;
    
	if (acc_motion == NULL)
		return -EINVAL;
	if (copy_from_user(&kacc_motion, acc_motion, sizeof(kacc_motion)))
		return -EFAULT;
	pr_info("dif_x=%d, dif_y=%d, dif_z=%d,frq=%d", kacc_motion.dlt_x,
            kacc_motion.dlt_y, kacc_motion.dlt_z, kacc_motion.frq);
	ev = kmalloc(sizeof(*ev), GFP_KERNEL);
	if (!ev)
		return -ENOMEM;
	ev->event_is_valid = 0;
	init_waitqueue_head(&ev->ev_waitq);
	atomic_set(&ev->event_count, 1);
	memcpy(&ev->acc_m, &kacc_motion, sizeof(kacc_motion));
	id = insert_ev(ev);
	pr_info("Event id %d", id);
	if (id < 0)
		kfree(ev);
	return id;
}

SYSCALL_DEFINE1(accevt_wait, int, event_id)
{
	DEFINE_WAIT(wt);
	struct acc_event *ev;
	int r = 0;
	int kind = 0;
    
	if (event_id < 1)
		return -EINVAL;
	spin_lock(&event_lock);
	ev = idr_find(&event_idr, event_id);
	spin_unlock(&event_lock);
	if (!ev) {
		pr_info("there is no event id %d for process %d",
				event_id, current->pid);
		return -EINVAL;
	}
	get_event(ev);
	prepare_to_wait(&ev->ev_waitq, &wt, TASK_INTERRUPTIBLE);
	while (1) {
		if (signal_pending(current)) {
			r = -EINTR;
			break;
		}
		if (!ev->event_is_valid)
			break;
		kind = check(ev);
		if (kind)
			break;
		put_event(ev);
		schedule();
		prepare_to_wait(&ev->ev_waitq, &wt, TASK_INTERRUPTIBLE);
	}
	finish_wait(&ev->ev_waitq, &wt);
	pr_info("Process %d woke up by event %d", current->pid, event_id);
	return r;
}

SYSCALL_DEFINE1(accevt_destroy, int, event_id)
{
	struct acc_event *ev;
	int n = 0;
    
	if (event_id < 0)
		return -EINVAL;
	pr_info("Try to destroy the event %d", event_id);
	spin_lock(&event_lock);
	ev = idr_find(&event_idr, event_id);
	if (ev) {
		idr_remove(&event_idr, event_id);
		ev->event_is_valid = 0;
		n = 1;
		pr_info("Successfuly destroyed the event %d", event_id);
	} else {
		pr_info("The event %d does not exist", event_id);
	}
    
	spin_unlock(&event_lock);
	return n;
}