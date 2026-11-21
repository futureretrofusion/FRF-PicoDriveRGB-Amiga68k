#ifndef FRF_COPROC_ABI_H
#define FRF_COPROC_ABI_H

#include <stdint.h>

#define FRF_COPROC_MAGIC                 0x434F5052UL /* 'COPR' */
#define FRF_COPROC_ABI_VERSION           1UL

/* Uses unused tail of existing 2 MiB FRFGFX shared reservation.
 * DirectAudio V0.3.1 ends at 0x000E1000. */
#define FRF_COPROC_CTRL_OFFSET           0x000E2000UL
#define FRF_COPROC_QUEUE_OFFSET          0x000E3000UL
#define FRF_COPROC_QUEUE_ENTRIES         16UL
#define FRF_COPROC_DESC_BYTES            64UL
#define FRF_COPROC_WORKSPACE_OFFSET      0x000E4000UL
#define FRF_COPROC_WORKSPACE_BYTES       0x00080000UL /* 512 KiB */
#define FRF_COPROC_REQUIRED_BYTES        (FRF_COPROC_WORKSPACE_OFFSET + FRF_COPROC_WORKSPACE_BYTES)

#define FRF_COPROC_DISCOVERY_BASE        0xFFF0D800UL
#define FRFCP_DISC_MAGIC                 (FRF_COPROC_DISCOVERY_BASE + 0x00)
#define FRFCP_DISC_ABI                   (FRF_COPROC_DISCOVERY_BASE + 0x04)
#define FRFCP_DISC_CTRL                  (FRF_COPROC_DISCOVERY_BASE + 0x08)
#define FRFCP_DISC_QUEUE                 (FRF_COPROC_DISCOVERY_BASE + 0x0C)
#define FRFCP_DISC_QUEUE_ENTRIES         (FRF_COPROC_DISCOVERY_BASE + 0x10)
#define FRFCP_DISC_WORKSPACE             (FRF_COPROC_DISCOVERY_BASE + 0x14)
#define FRFCP_DISC_WORKSPACE_BYTES       (FRF_COPROC_DISCOVERY_BASE + 0x18)
#define FRFCP_DISC_FEATURES              (FRF_COPROC_DISCOVERY_BASE + 0x1C)
#define FRFCP_DISC_DOORBELL              (FRF_COPROC_DISCOVERY_BASE + 0x20)
#define FRFCP_DISC_WORKER_CORE           (FRF_COPROC_DISCOVERY_BASE + 0x24)
#define FRFCP_DISC_END                   (FRF_COPROC_DISCOVERY_BASE + 0x28)
#define FRF_COPROC_DOORBELL_ADDR         0xFFF0D840UL

#define FRFCP_FEAT_ASYNC_QUEUE           (1UL << 0)
#define FRFCP_FEAT_ARM_CORE3             (1UL << 1)
#define FRFCP_FEAT_WORKSPACE             (1UL << 2)
#define FRFCP_FEAT_MEMCOPY               (1UL << 3)
#define FRFCP_FEAT_MEMFILL               (1UL << 4)
#define FRFCP_FEAT_CRC32                 (1UL << 5)
#define FRFCP_FEAT_PCM_SPLIT_S8          (1UL << 6)
#define FRFCP_FEAT_RGBA_TO_RGB565        (1UL << 7)
#define FRFCP_FEAT_FIXED_DISCOVERY       (1UL << 8)
#define FRFCP_FEAT_RGB565_TO_HAM6_PLANAR (1UL << 9)

#define FRFCP_JOB_FREE                   0UL
#define FRFCP_JOB_READY                  1UL
#define FRFCP_JOB_RUNNING                2UL
#define FRFCP_JOB_DONE                   3UL
#define FRFCP_JOB_ERROR                  4UL

#define FRFCP_OP_NOP                     0UL
#define FRFCP_OP_MEMCOPY                 1UL
#define FRFCP_OP_MEMFILL8                2UL
#define FRFCP_OP_CRC32                   3UL
#define FRFCP_OP_S16BE_STEREO_TO_S8_SPLIT 4UL
#define FRFCP_OP_RGBA8888_TO_RGB565_BE   5UL
#define FRFCP_OP_RGB565_TO_HAM6_PLANAR   6UL

#define FRFCP_STATUS_OK                  0UL
#define FRFCP_STATUS_BAD_ABI             1UL
#define FRFCP_STATUS_BAD_OPCODE          2UL
#define FRFCP_STATUS_BAD_RANGE           3UL
#define FRFCP_STATUS_BAD_SIZE            4UL
#define FRFCP_STATUS_NOT_READY           5UL

struct FRFCoprocControl {
    uint32_t magic;
    uint32_t abi_version;
    uint32_t struct_size;
    uint32_t features;

    uint32_t queue_base;
    uint32_t queue_entries;
    uint32_t workspace_base;
    uint32_t workspace_bytes;

    uint32_t doorbell_addr;
    uint32_t producer;
    uint32_t consumer;
    uint32_t completed;

    uint32_t errors;
    uint32_t worker_online;
    uint32_t worker_core;
    uint32_t counter_hz;

    uint32_t wakeups;
    uint32_t jobs_started;
    uint32_t bytes_processed_lo;
    uint32_t bytes_processed_hi;

    uint32_t reserved[12];
};

struct FRFCoprocJob {
    uint32_t state;
    uint32_t job_id;
    uint32_t opcode;
    uint32_t status;

    uint32_t src_offset;
    uint32_t dst_offset;
    uint32_t bytes;
    uint32_t arg0;

    uint32_t arg1;
    uint32_t result0;
    uint32_t result1;
    uint32_t cycles_lo;

    uint32_t cycles_hi;
    uint32_t reserved0;
    uint32_t reserved1;
    uint32_t reserved2;
};

#define FRF_COPROC_CTRL_BYTES ((uint32_t)sizeof(struct FRFCoprocControl))
#define FRF_COPROC_JOB_BYTES  ((uint32_t)sizeof(struct FRFCoprocJob))

#endif
