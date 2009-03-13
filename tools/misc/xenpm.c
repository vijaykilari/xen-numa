/*
 * xenpm.c: list the power information of the available processors
 * Copyright (c) 2008, Intel Corporation.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc., 59 Temple
 * Place - Suite 330, Boston, MA 02111-1307 USA.
 */

/* to eliminate warning on `strndup' */
#define _GNU_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <getopt.h>
#include <errno.h>
#include <signal.h>

#include <xenctrl.h>
#include <inttypes.h>
#include <sys/time.h>

#define ARRAY_SIZE(a) (sizeof (a) / sizeof ((a)[0]))

static int xc_fd;
static int max_cpu_nr;

/* help message */
void show_help(void)
{
    fprintf(stderr,
            "xen power management control tool\n\n"
            "usage: xenpm <command> [args]\n\n"
            "xenpm command list:\n\n"
            " get-cpuidle-states    [cpuid]       list cpu idle info of CPU <cpuid> or all\n"
            " get-cpufreq-states    [cpuid]       list cpu freq info of CPU <cpuid> or all\n"
            " get-cpufreq-para      [cpuid]       list cpu freq parameter of CPU <cpuid> or all\n"
            " set-scaling-maxfreq   [cpuid] <HZ>  set max cpu frequency <HZ> on CPU <cpuid>\n"
            "                                     or all CPUs\n"
            " set-scaling-minfreq   [cpuid] <HZ>  set min cpu frequency <HZ> on CPU <cpuid>\n"
            "                                     or all CPUs\n"
            " set-scaling-speed     [cpuid] <num> set scaling speed on CPU <cpuid> or all\n"
            "                                     it is used in userspace governor.\n"
            " set-scaling-governor  [cpuid] <gov> set scaling governor on CPU <cpuid> or all\n"
            "                                     as userspace/performance/powersave/ondemand\n"
            " set-sampling-rate     [cpuid] <num> set sampling rate on CPU <cpuid> or all\n"
            "                                     it is used in ondemand governor.\n"
            " set-up-threshold      [cpuid] <num> set up threshold on CPU <cpuid> or all\n"
            "                                     it is used in ondemand governor.\n"
            " get-cpu-topology                    get thread/core/socket topology info\n"
            " start                               start collect Cx/Px statistics,\n"
            "                                     output after CTRL-C or SIGINT.\n"
            );
}
/* wrapper function */
void help_func(int argc, char *argv[])
{
    show_help();
}

static void print_cxstat(int cpuid, struct xc_cx_stat *cxstat)
{
    int i;

    printf("cpu id               : %d\n", cpuid);
    printf("total C-states       : %d\n", cxstat->nr);
    printf("idle time(ms)        : %"PRIu64"\n",
           cxstat->idle_time/1000000UL);
    for ( i = 0; i < cxstat->nr; i++ )
    {
        printf("C%d                   : transition [%020"PRIu64"]\n",
               i, cxstat->triggers[i]);
        printf("                       residency  [%020"PRIu64" ms]\n",
               cxstat->residencies[i]/1000000UL);
    }
    printf("\n");
}

/* show cpu idle information on CPU cpuid */
static int get_cxstat_by_cpuid(int xc_fd, int cpuid, struct xc_cx_stat *cxstat)
{
    int ret = 0;
    int max_cx_num = 0;

    ret = xc_pm_get_max_cx(xc_fd, cpuid, &max_cx_num);
    if ( ret )
        return errno;

    if ( !cxstat )
        return -EINVAL;

    cxstat->triggers = malloc(max_cx_num * sizeof(uint64_t));
    if ( !cxstat->triggers )
        return -ENOMEM;
    cxstat->residencies = malloc(max_cx_num * sizeof(uint64_t));
    if ( !cxstat->residencies )
    {
        free(cxstat->triggers);
        return -ENOMEM;
    }

    ret = xc_pm_get_cxstat(xc_fd, cpuid, cxstat);
    if( ret )
    {
        int temp = errno;
        free(cxstat->triggers);
        free(cxstat->residencies);
        cxstat->triggers = NULL;
        cxstat->residencies = NULL;
        return temp;
    }

    return 0;
}

static int show_cxstat_by_cpuid(int xc_fd, int cpuid)
{
    int ret = 0;
    struct xc_cx_stat cxstatinfo;

    ret = get_cxstat_by_cpuid(xc_fd, cpuid, &cxstatinfo);
    if ( ret )
        return ret;

    print_cxstat(cpuid, &cxstatinfo);

    free(cxstatinfo.triggers);
    free(cxstatinfo.residencies);
    return 0;
}

void cxstat_func(int argc, char *argv[])
{
    int cpuid = -1;

    if ( argc > 0 && sscanf(argv[0], "%d", &cpuid) != 1 )
        cpuid = -1;

    if ( cpuid >= max_cpu_nr )
        cpuid = -1;

    if ( cpuid < 0 )
    {
        /* show cxstates on all cpus */
        int i;
        for ( i = 0; i < max_cpu_nr; i++ )
            if ( show_cxstat_by_cpuid(xc_fd, i) == -ENODEV )
                break;
    }
    else
        show_cxstat_by_cpuid(xc_fd, cpuid);
}

static void print_pxstat(int cpuid, struct xc_px_stat *pxstat)
{
    int i;

    printf("cpu id               : %d\n", cpuid);
    printf("total P-states       : %d\n", pxstat->total);
    printf("usable P-states      : %d\n", pxstat->usable);
    printf("current frequency    : %"PRIu64" MHz\n",
           pxstat->pt[pxstat->cur].freq);
    for ( i = 0; i < pxstat->total; i++ )
    {
        if ( pxstat->cur == i )
            printf("*P%d", i);
        else
            printf("P%d ", i);
        printf("                  : freq       [%04"PRIu64" MHz]\n",
               pxstat->pt[i].freq);
        printf("                       transition [%020"PRIu64"]\n",
               pxstat->pt[i].count);
        printf("                       residency  [%020"PRIu64" ms]\n",
               pxstat->pt[i].residency/1000000UL);
    }
    printf("\n");
}

/* show cpu frequency information on CPU cpuid */
static int get_pxstat_by_cpuid(int xc_fd, int cpuid, struct xc_px_stat *pxstat)
{
    int ret = 0;
    int max_px_num = 0;

    ret = xc_pm_get_max_px(xc_fd, cpuid, &max_px_num);
    if ( ret )
        return errno;

    if ( !pxstat)
        return -EINVAL;

    pxstat->trans_pt = malloc(max_px_num * max_px_num *
                              sizeof(uint64_t));
    if ( !pxstat->trans_pt )
        return -ENOMEM;
    pxstat->pt = malloc(max_px_num * sizeof(struct xc_px_val));
    if ( !pxstat->pt )
    {
        free(pxstat->trans_pt);
        return -ENOMEM;
    }

    ret = xc_pm_get_pxstat(xc_fd, cpuid, pxstat);
    if( ret )
    {
        int temp = errno;
        free(pxstat->trans_pt);
        free(pxstat->pt);
        pxstat->trans_pt = NULL;
        pxstat->pt = NULL;
        return temp;
    }

    return 0;
}

static int show_pxstat_by_cpuid(int xc_fd, int cpuid)
{
    int ret = 0;
    struct xc_px_stat pxstatinfo;

    ret = get_pxstat_by_cpuid(xc_fd, cpuid, &pxstatinfo);
    if ( ret )
        return ret;

    print_pxstat(cpuid, &pxstatinfo);

    free(pxstatinfo.trans_pt);
    free(pxstatinfo.pt);
    return 0;
}

void pxstat_func(int argc, char *argv[])
{
    int cpuid = -1;

    if ( argc > 0 && sscanf(argv[0], "%d", &cpuid) != 1 )
        cpuid = -1;

    if ( cpuid >= max_cpu_nr )
        cpuid = -1;

    if ( cpuid < 0 )
    {
        /* show pxstates on all cpus */
        int i;
        for ( i = 0; i < max_cpu_nr; i++ )
            if ( show_pxstat_by_cpuid(xc_fd, i) == -ENODEV )
                break;
    }
    else
        show_pxstat_by_cpuid(xc_fd, cpuid);
}

static uint64_t usec_start, usec_end;
static struct xc_cx_stat *cxstat, *cxstat_start, *cxstat_end;
static struct xc_px_stat *pxstat, *pxstat_start, *pxstat_end;
static uint64_t *sum, *sum_cx, *sum_px;

static void signal_int_handler(int signo)
{
    int i, j;
    struct timeval tv;
    int cx_cap = 0, px_cap = 0;

    if ( gettimeofday(&tv, NULL) == -1 )
    {
        fprintf(stderr, "failed to get timeofday\n");
        return ;
    }
    usec_end = tv.tv_sec * 1000000UL + tv.tv_usec;

    if ( get_cxstat_by_cpuid(xc_fd, 0, NULL) != -ENODEV )
    {
        cx_cap = 1;
        for ( i = 0; i < max_cpu_nr; i++ )
            if ( !get_cxstat_by_cpuid(xc_fd, i, &cxstat_end[i]) )
                for ( j = 0; j < cxstat_end[i].nr; j++ )
                    sum_cx[i] += cxstat_end[i].residencies[j] -
                                 cxstat_start[i].residencies[j];
    }

    if ( get_pxstat_by_cpuid(xc_fd, 0, NULL) != -ENODEV )
    {
        px_cap = 1;
        for ( i = 0; i < max_cpu_nr; i++ )
            if ( !get_pxstat_by_cpuid(xc_fd, i , &pxstat_end[i]) )
                for ( j = 0; j < pxstat_end[i].total; j++ )
                    sum_px[i] += pxstat_end[i].pt[j].residency -
                                 pxstat_start[i].pt[j].residency;
    }

    printf("Elapsed time (ms): %"PRIu64"\n", (usec_end - usec_start) / 1000UL);
    for ( i = 0; i < max_cpu_nr; i++ )
    {
        uint64_t res, triggers;
        double avg_res;

        printf("\nCPU%d:\tResidency(ms)\t\tAvg Res(ms)\n",i);
        if ( cx_cap && sum_cx[i] > 0 )
        {
            for ( j = 0; j < cxstat_end[i].nr; j++ )
            {
                res = cxstat_end[i].residencies[j] -
                    cxstat_start[i].residencies[j];
                triggers = cxstat_end[i].triggers[j] -
                    cxstat_start[i].triggers[j];
                avg_res = (triggers==0) ? 0: (double)res/triggers/1000000.0;
                printf("  C%d\t%"PRIu64"\t(%5.2f%%)\t%.2f\n", j, res/1000000UL,
                        100 * res / (double)sum_cx[i], avg_res );
            }
            printf("\n");
        }
        if ( px_cap && sum_px[i]>0 )
        {
            for ( j = 0; j < pxstat_end[i].total; j++ )
            {
                res = pxstat_end[i].pt[j].residency -
                    pxstat_start[i].pt[j].residency;
                printf("  P%d\t%"PRIu64"\t(%5.2f%%)\n", j,
                        res / 1000000UL, 100UL * res / (double)sum_px[i]);
            }
        }
    }

    /* some clean up and then exits */
    for ( i = 0; i < 2 * max_cpu_nr; i++ )
    {
        free(cxstat[i].triggers);
        free(cxstat[i].residencies);
        free(pxstat[i].trans_pt);
        free(pxstat[i].pt);
    }
    free(cxstat);
    free(pxstat);
    free(sum);
    xc_interface_close(xc_fd);
    exit(0);
}

void start_gather_func(int argc, char *argv[])
{
    int i;
    struct timeval tv;

    if ( gettimeofday(&tv, NULL) == -1 )
    {
        fprintf(stderr, "failed to get timeofday\n");
        return ;
    }
    usec_start = tv.tv_sec * 1000000UL + tv.tv_usec;

    sum = malloc(sizeof(uint64_t) * 2 * max_cpu_nr);
    if ( sum == NULL )
        return ;
    cxstat = malloc(sizeof(struct xc_cx_stat) * 2 * max_cpu_nr);
    if ( cxstat == NULL )
    {
        free(sum);
        return ;
    }
    pxstat = malloc(sizeof(struct xc_px_stat) * 2 * max_cpu_nr);
    if ( pxstat == NULL )
    {
        free(sum);
        free(cxstat);
        return ;
    }
    memset(sum, 0, sizeof(uint64_t) * 2 * max_cpu_nr);
    memset(cxstat, 0, sizeof(struct xc_cx_stat) * 2 * max_cpu_nr);
    memset(pxstat, 0, sizeof(struct xc_px_stat) * 2 * max_cpu_nr);
    sum_cx = sum;
    sum_px = sum + max_cpu_nr;
    cxstat_start = cxstat;
    cxstat_end = cxstat + max_cpu_nr;
    pxstat_start = pxstat;
    pxstat_end = pxstat + max_cpu_nr;

    if ( get_cxstat_by_cpuid(xc_fd, 0, NULL) == -ENODEV &&
         get_pxstat_by_cpuid(xc_fd, 0, NULL) == -ENODEV )
    {
        fprintf(stderr, "Xen cpu idle and frequency is disabled!\n");
        return ;
    }

    for ( i = 0; i < max_cpu_nr; i++ )
    {
        get_cxstat_by_cpuid(xc_fd, i, &cxstat_start[i]);
        get_pxstat_by_cpuid(xc_fd, i, &pxstat_start[i]);
    }

    if (signal(SIGINT, signal_int_handler) == SIG_ERR)
    {
        fprintf(stderr, "failed to set signal int handler\n");
        free(sum);
        free(pxstat);
        free(cxstat);
        return ;
    }
    printf("Start sampling, waiting for CTRL-C or SIGINT signal ...\n");

    pause();
}

/* print out parameters about cpu frequency */
static void print_cpufreq_para(int cpuid, struct xc_get_cpufreq_para *p_cpufreq)
{
    int i;

    printf("cpu id               : %d\n", cpuid);

    printf("affected_cpus        :");
    for ( i = 0; i < p_cpufreq->cpu_num; i++ )
        if ( i == cpuid )
            printf(" *%d", p_cpufreq->affected_cpus[i]);
        else
            printf(" %d", p_cpufreq->affected_cpus[i]);
    printf("\n");

    printf("cpuinfo frequency    : max [%u] min [%u] cur [%u]\n",
           p_cpufreq->cpuinfo_max_freq,
           p_cpufreq->cpuinfo_min_freq,
           p_cpufreq->cpuinfo_cur_freq);

    printf("scaling_driver       : %s\n", p_cpufreq->scaling_driver);

    printf("scaling_avail_gov    : %s\n",
           p_cpufreq->scaling_available_governors);

    printf("current_governor     : %s\n", p_cpufreq->scaling_governor);
    if ( !strncmp(p_cpufreq->scaling_governor,
                  "userspace", CPUFREQ_NAME_LEN) )
    {
        printf("  userspace specific :\n");
        printf("    scaling_setspeed : %u\n",
               p_cpufreq->u.userspace.scaling_setspeed);
    }
    else if ( !strncmp(p_cpufreq->scaling_governor,
                       "ondemand", CPUFREQ_NAME_LEN) )
    {
        printf("  ondemand specific  :\n");
        printf("    sampling_rate    : max [%u] min [%u] cur [%u]\n",
               p_cpufreq->u.ondemand.sampling_rate_max,
               p_cpufreq->u.ondemand.sampling_rate_min,
               p_cpufreq->u.ondemand.sampling_rate);
        printf("    up_threshold     : %u\n",
               p_cpufreq->u.ondemand.up_threshold);
    }

    printf("scaling_avail_freq   :");
    for ( i = 0; i < p_cpufreq->freq_num; i++ )
        if ( p_cpufreq->scaling_available_frequencies[i] ==
             p_cpufreq->scaling_cur_freq )
            printf(" *%d", p_cpufreq->scaling_available_frequencies[i]);
        else
            printf(" %d", p_cpufreq->scaling_available_frequencies[i]);
    printf("\n");

    printf("scaling frequency    : max [%u] min [%u] cur [%u]\n",
           p_cpufreq->scaling_max_freq,
           p_cpufreq->scaling_min_freq,
           p_cpufreq->scaling_cur_freq);
    printf("\n");
}

/* show cpu frequency parameters information on CPU cpuid */
static int show_cpufreq_para_by_cpuid(int xc_fd, int cpuid)
{
    int ret = 0;
    struct xc_get_cpufreq_para cpufreq_para, *p_cpufreq = &cpufreq_para;

    p_cpufreq->cpu_num = 0;
    p_cpufreq->freq_num = 0;
    p_cpufreq->gov_num = 0;
    p_cpufreq->affected_cpus = NULL;
    p_cpufreq->scaling_available_frequencies = NULL;
    p_cpufreq->scaling_available_governors = NULL;

    do
    {
        free(p_cpufreq->affected_cpus);
        free(p_cpufreq->scaling_available_frequencies);
        free(p_cpufreq->scaling_available_governors);

        p_cpufreq->affected_cpus = NULL;
        p_cpufreq->scaling_available_frequencies = NULL;
        p_cpufreq->scaling_available_governors = NULL;

        if (!(p_cpufreq->affected_cpus =
              malloc(p_cpufreq->cpu_num * sizeof(uint32_t))))
        {
            fprintf(stderr,
                    "[CPU%d] failed to malloc for affected_cpus\n",
                    cpuid);
            ret = -ENOMEM;
            goto out;
        }
        if (!(p_cpufreq->scaling_available_frequencies =
              malloc(p_cpufreq->freq_num * sizeof(uint32_t))))
        {
            fprintf(stderr,
                    "[CPU%d] failed to malloc for scaling_available_frequencies\n",
                    cpuid);
            ret = -ENOMEM;
            goto out;
        }
        if (!(p_cpufreq->scaling_available_governors =
              malloc(p_cpufreq->gov_num * CPUFREQ_NAME_LEN * sizeof(char))))
        {
            fprintf(stderr,
                    "[CPU%d] failed to malloc for scaling_available_governors\n",
                    cpuid);
            ret = -ENOMEM;
            goto out;
        }

        ret = xc_get_cpufreq_para(xc_fd, cpuid, p_cpufreq);
    } while ( ret && errno == EAGAIN );

    if ( ret == 0 )
        print_cpufreq_para(cpuid, p_cpufreq);
    else if ( errno == ENODEV )
    {
        ret = -ENODEV;
        fprintf(stderr, "Xen cpufreq is not enabled!\n");
    }
    else
        fprintf(stderr,
                "[CPU%d] failed to get cpufreq parameter\n",
                cpuid);

out:
    free(p_cpufreq->scaling_available_governors);
    free(p_cpufreq->scaling_available_frequencies);
    free(p_cpufreq->affected_cpus);

    return ret;
}

void cpufreq_para_func(int argc, char *argv[])
{
    int cpuid = -1;

    if ( argc > 0 && sscanf(argv[0], "%d", &cpuid) != 1 )
        cpuid = -1;

    if ( cpuid >= max_cpu_nr )
        cpuid = -1;

    if ( cpuid < 0 )
    {
        /* show cpu freqency information on all cpus */
        int i;
        for ( i = 0; i < max_cpu_nr; i++ )
            if ( show_cpufreq_para_by_cpuid(xc_fd, i) == -ENODEV )
                break;
    }
    else
        show_cpufreq_para_by_cpuid(xc_fd, cpuid);
}

void scaling_max_freq_func(int argc, char *argv[])
{
    int cpuid = -1, freq = -1;

    if ( (argc >= 2 && (sscanf(argv[1], "%d", &freq) != 1 ||
                        sscanf(argv[0], "%d", &cpuid) != 1)) ||
         (argc == 1 && sscanf(argv[0], "%d", &freq) != 1 ) ||
         argc == 0 )
    {
        fprintf(stderr, "failed to set scaling max freq\n");
        return ;
    }

    if ( cpuid < 0 )
    {
        int i;
        for ( i = 0; i < max_cpu_nr; i++ )
            if ( xc_set_cpufreq_para(xc_fd, i, SCALING_MAX_FREQ, freq) )
                fprintf(stderr, "[CPU%d] failed to set scaling max freq\n", i);
    }
    else
    {
        if ( xc_set_cpufreq_para(xc_fd, cpuid, SCALING_MAX_FREQ, freq) )
            fprintf(stderr, "failed to set scaling max freq\n");
    }
}

void scaling_min_freq_func(int argc, char *argv[])
{
    int cpuid = -1, freq = -1;

    if ( (argc >= 2 && (sscanf(argv[1], "%d", &freq) != 1 ||
                        sscanf(argv[0], "%d", &cpuid) != 1) ) ||
         (argc == 1 && sscanf(argv[0], "%d", &freq) != 1 ) ||
         argc == 0 )
    {
        fprintf(stderr, "failed to set scaling min freq\n");
        return ;
    }

    if ( cpuid < 0 )
    {
        int i;
        for ( i = 0; i < max_cpu_nr; i++ )
            if ( xc_set_cpufreq_para(xc_fd, i, SCALING_MIN_FREQ, freq) )
                fprintf(stderr, "[CPU%d] failed to set scaling min freq\n", i);
    }
    else
    {
        if ( xc_set_cpufreq_para(xc_fd, cpuid, SCALING_MIN_FREQ, freq) )
            fprintf(stderr, "failed to set scaling min freq\n");
    }
}

void scaling_speed_func(int argc, char *argv[])
{
    int cpuid = -1, speed = -1;

    if ( (argc >= 2 && (sscanf(argv[1], "%d", &speed) != 1 ||
                        sscanf(argv[0], "%d", &cpuid) != 1) ) ||
         (argc == 1 && sscanf(argv[0], "%d", &speed) != 1 ) ||
         argc == 0 )
    {
        fprintf(stderr, "failed to set scaling speed\n");
        return ;
    }

    if ( cpuid < 0 )
    {
        int i;
        for ( i = 0; i < max_cpu_nr; i++ )
            if ( xc_set_cpufreq_para(xc_fd, i, SCALING_SETSPEED, speed) )
                fprintf(stderr, "[CPU%d] failed to set scaling speed\n", i);
    }
    else
    {
        if ( xc_set_cpufreq_para(xc_fd, cpuid, SCALING_SETSPEED, speed) )
            fprintf(stderr, "failed to set scaling speed\n");
    }
}

void scaling_sampling_rate_func(int argc, char *argv[])
{
    int cpuid = -1, rate = -1;

    if ( (argc >= 2 && (sscanf(argv[1], "%d", &rate) != 1 ||
                        sscanf(argv[0], "%d", &cpuid) != 1) ) ||
         (argc == 1 && sscanf(argv[0], "%d", &rate) != 1 ) ||
         argc == 0 )
    {
        fprintf(stderr, "failed to set scaling sampling rate\n");
        return ;
    }

    if ( cpuid < 0 )
    {
        int i;
        for ( i = 0; i < max_cpu_nr; i++ )
            if ( xc_set_cpufreq_para(xc_fd, i, SAMPLING_RATE, rate) )
                fprintf(stderr,
                        "[CPU%d] failed to set scaling sampling rate\n", i);
    }
    else
    {
        if ( xc_set_cpufreq_para(xc_fd, cpuid, SAMPLING_RATE, rate) )
            fprintf(stderr, "failed to set scaling sampling rate\n");
    }
}

void scaling_up_threshold_func(int argc, char *argv[])
{
    int cpuid = -1, threshold = -1;

    if ( (argc >= 2 && (sscanf(argv[1], "%d", &threshold) != 1 ||
                        sscanf(argv[0], "%d", &cpuid) != 1) ) ||
         (argc == 1 && sscanf(argv[0], "%d", &threshold) != 1 ) ||
         argc == 0 )
    {
        fprintf(stderr, "failed to set up scaling threshold\n");
        return ;
    }

    if ( cpuid < 0 )
    {
        int i;
        for ( i = 0; i < max_cpu_nr; i++ )
            if ( xc_set_cpufreq_para(xc_fd, i, UP_THRESHOLD, threshold) )
                fprintf(stderr,
                        "[CPU%d] failed to set up scaling threshold\n", i);
    }
    else
    {
        if ( xc_set_cpufreq_para(xc_fd, cpuid, UP_THRESHOLD, threshold) )
            fprintf(stderr, "failed to set up scaling threshold\n");
    }
}

void scaling_governor_func(int argc, char *argv[])
{
    int cpuid = -1;
    char *name = NULL;

    if ( argc >= 2 )
    {
        name = strdup(argv[1]);
        if ( name == NULL )
            goto out;
        if ( sscanf(argv[0], "%d", &cpuid) != 1 )
        {
            free(name);
            goto out;
        }
    }
    else if ( argc > 0 )
    {
        name = strdup(argv[0]);
        if ( name == NULL )
            goto out;
    }
    else
        goto out;

    if ( cpuid < 0 )
    {
        int i;
        for ( i = 0; i < max_cpu_nr; i++ )
            if ( xc_set_cpufreq_gov(xc_fd, i, name) )
                fprintf(stderr, "[CPU%d] failed to set governor name\n", i);
    }
    else
    {
        if ( xc_set_cpufreq_gov(xc_fd, cpuid, name) )
            fprintf(stderr, "failed to set governor name\n");
    }

    free(name);
    return ;
out:
    fprintf(stderr, "failed to set governor name\n");
}

#define MAX_NR_CPU 512

void cpu_topology_func(int argc, char *argv[])
{
    uint32_t cpu_to_core[MAX_NR_CPU];
    uint32_t cpu_to_socket[MAX_NR_CPU];
    struct xc_get_cputopo info;
    int i, ret;

    info.cpu_to_core = cpu_to_core;
    info.cpu_to_socket = cpu_to_socket;
    info.max_cpus = MAX_NR_CPU;
    ret = xc_get_cputopo(xc_fd, &info);
    if (!ret)
    {
        printf("CPU\tcore\tsocket\n");
        for (i=0; i<info.nr_cpus; i++)
        {
            if ( info.cpu_to_core[i] != INVALID_TOPOLOGY_ID &&
                    info.cpu_to_socket[i] != INVALID_TOPOLOGY_ID )
            {
            printf("CPU%d\t %d\t %d\n", i, info.cpu_to_core[i],
                    info.cpu_to_socket[i]);
            }
        }
    }
    else
    {
        printf("Can not get Xen CPU topology!\n");
    }

    return ;
}

struct {
    const char *name;
    void (*function)(int argc, char *argv[]);
} main_options[] = {
    { "help", help_func },
    { "get-cpuidle-states", cxstat_func },
    { "get-cpufreq-states", pxstat_func },
    { "start", start_gather_func },
    { "get-cpufreq-para", cpufreq_para_func },
    { "set-scaling-maxfreq", scaling_max_freq_func },
    { "set-scaling-minfreq", scaling_min_freq_func },
    { "set-scaling-governor", scaling_governor_func },
    { "set-scaling-speed", scaling_speed_func },
    { "set-sampling-rate", scaling_sampling_rate_func },
    { "set-up-threshold", scaling_up_threshold_func },
    { "get-cpu-topology", cpu_topology_func},
};

int main(int argc, char *argv[])
{
    int i, ret = 0;
    xc_physinfo_t physinfo = { 0 };
    int nr_matches = 0;
    int matches_main_options[ARRAY_SIZE(main_options)];

    if ( argc < 2 )
    {
        show_help();
        return 0;
    }

    xc_fd = xc_interface_open();
    if ( xc_fd < 0 )
    {
        fprintf(stderr, "failed to get the handler\n");
        return 0;
    }

    ret = xc_physinfo(xc_fd, &physinfo);
    if ( ret )
    {
        fprintf(stderr, "failed to get the processor information\n");
        xc_interface_close(xc_fd);
        return 0;
    }
    max_cpu_nr = physinfo.nr_cpus;

    /* calculate how many options match with user's input */
    for ( i = 0; i < ARRAY_SIZE(main_options); i++ )
        if ( !strncmp(main_options[i].name, argv[1], strlen(argv[1])) )
            matches_main_options[nr_matches++] = i;

    if ( nr_matches > 1 )
    {
        fprintf(stderr, "Ambigious options: ");
        for ( i = 0; i < nr_matches; i++ )
            fprintf(stderr, " %s", main_options[matches_main_options[i]].name);
        fprintf(stderr, "\n");
    }
    else if ( nr_matches == 1 )
        /* dispatch to the corresponding function handler */
        main_options[matches_main_options[0]].function(argc - 2, argv + 2);
    else
        show_help();

    xc_interface_close(xc_fd);
    return 0;
}

