/*
 * telephony utils
 *
 * This software program is licensed subject to the GNU General Public License
 * (GPL).Version 2,June 1991, available at http://www.fsf.org/copyleft/gpl.html

 * (C) Copyright 2015 Marvell International Ltd.
 * All Rights Reserved
 */
#ifndef TEL_UTIL_H_
#define TEL_UTIL_H_

#define DEFINE_BLOCKING_NOTIFIER(name) \
static BLOCKING_NOTIFIER_HEAD(name##_notifier_list); \
\
static inline int notify_##name(unsigned long val, void *v) \
{ \
	return blocking_notifier_call_chain(&name##_notifier_list, val, v); \
} \
\
int register_##name##_notifier(struct notifier_block *nb) \
{ \
	return blocking_notifier_chain_register(&name##_notifier_list, nb); \
} \
EXPORT_SYMBOL(register_##name##_notifier); \
\
int unregister_##name##_notifier(struct notifier_block *nb) \
{ \
	return blocking_notifier_chain_unregister(&name##_notifier_list, nb); \
} \
EXPORT_SYMBOL(unregister_##name##_notifier)

#define DECLARE_BLOCKING_NOTIFIER(name) \
extern int register_##name##_notifier(struct notifier_block *nb); \
extern int unregister_##name##_notifier(struct notifier_block *nb)

#endif /* TEL_UTIL_H_ */
