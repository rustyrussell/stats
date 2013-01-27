#include <ccan/err/err.h>
#include <ccan/opt/opt.h>
#include <ccan/rbuf/rbuf.h>
#include <ccan/htable/htable_type.h>
#include <ccan/hash/hash.h>
#include <ccan/list/list.h>
#include <ccan/str/str.h>
#include <unistd.h>
#include <errno.h>

enum pattern_type {
	LITERAL,
	INTEGER,
	FLOAT,
	/* These are parsing states. */
	PRESPACES,
	TERM
};

struct pattern_part {
	enum pattern_type type;
	size_t len;
	union {
		const char *sval;
		long long ival;
		double dval;
	} u;
};

struct pattern {
	size_t num_parts;
	struct pattern_part part[ /* num_parts */ ];
};

struct line {
	struct list_node list;
	struct pattern *pattern;
	size_t count;

	struct pattern_part *min, *max, *total;
};

static const struct pattern *line_key(const struct line *line)
{
	return line->pattern;
}

static size_t pattern_hash(const struct pattern *p)
{
	size_t i;
	size_t h = p->num_parts; 

	for (i = 0; i < p->num_parts; i++) {
		const struct pattern_part *part = &p->part[i];

		if (part->type == LITERAL)
			h = hash(part->u.sval, part->len, h);
	}
	return h;
}

static bool line_eq(const struct line *line, const struct pattern *p)
{
	const struct pattern *p2 = line->pattern;
	size_t i;

	if (p->num_parts != p2->num_parts)
		return false;
	for (i = 0; i < p->num_parts; i++) {
		const struct pattern_part *part1 = &p->part[i];
		const struct pattern_part *part2 = &p2->part[i];

		if (part1->type == LITERAL) {
			if (part2->type != LITERAL)
				return false;
			if (part1->len != part2->len)
				return false;
			if (strncmp(part1->u.sval, part2->u.sval, part1->len))
				return false;
		} else if (part2->type == LITERAL)
			return false;
	}
	return true;
}

HTABLE_DEFINE_TYPE(struct line, line_key, pattern_hash, line_eq, linehash);

struct file {
	struct list_head lines;
	struct linehash patterns;
};

static inline size_t partsize(size_t num)
{
	return sizeof(struct pattern) + sizeof(struct pattern_part) * num;
}

static void add_part(struct pattern **p, const struct pattern_part *part,
		     size_t *max_parts)
{
	if ((*p)->num_parts == *max_parts) {
		*max_parts *= 2;
		*p = realloc(*p, partsize(*max_parts));
	}
	(*p)->part[(*p)->num_parts++] = *part;
}

/* We want "finished in100 seconds to match "finished in  5 seconds". */
struct pattern *get_pattern(const char *line)
{
	enum pattern_type state = LITERAL;
	size_t len, i, max_parts = 3;
	struct pattern_part part;
	struct pattern *p;

	p = malloc(partsize(max_parts));
	p->num_parts = 0;

	for (i = len = 0; state != TERM; i++, len++) {
		enum pattern_type old_state = state;
		bool starts_num;

		starts_num = (line[i] == '-' && cisdigit(line[i+1]))
			|| cisdigit(line[i]);

		switch (state) {
		case LITERAL:
			if (starts_num) {
				state = INTEGER;
				break;
			} else if (cisspace(line[i])) {
				state = PRESPACES;
				break;
			}
			break;
		case PRESPACES:
			if (starts_num) {
				state = INTEGER;
				break;
			} else if (!cisspace(line[i])) {
				state = LITERAL;
			}
			break;
		case INTEGER:
			if (line[i] == '.') {
				if (cisdigit(line[i+1])) {
					/* Was float all along... */
					state = old_state = FLOAT;
				} else
					state = LITERAL;
				break;
			}
			/* fall thru */
		case FLOAT:
			if (cisspace(line[i])) {
				state = PRESPACES;
				break;
			} else if (!cisdigit(line[i])) {
				state = LITERAL;
				break;
			}
			break;
		case TERM:
			abort();
		}

		if (!line[i])
			state = TERM;

		if (state == old_state)
			continue;

		part.type = old_state;
		part.len = len;
		if (old_state == FLOAT) {
			char *end;
			part.u.dval = strtod(line + i - len, &end);
			if (end != line + i) {
				warnx("Could not parse float '%.*s'",
				      (int)len, line + i - len);
			} else {
				add_part(&p, &part, &max_parts);
			}
			len = 0;
		} else if (old_state == INTEGER) {
			char *end;
			part.u.ival = strtoll(line + i - len, &end, 10);
			if (end != line + i) {
				warnx("Could not parse integer '%.*s'",
				      (int)len, line + i - len);
			} else {
				add_part(&p, &part, &max_parts);
			}
			len = 0;
		} else if (old_state == LITERAL && len > 0) {
			/* Since we can go to PRESPACES and back, we can
			 * have successive literals.  Collapse them. */
			if (p->num_parts > 0
			    && p->part[p->num_parts-1].type == LITERAL) {
				p->part[p->num_parts-1].len += len;
				len = 0;
				continue;
			}
			part.u.sval = line + i - len;
			add_part(&p, &part, &max_parts);
			len = 0;
		}
	}
	return p;
}

static void convert_to_float(struct pattern_part *part)
{
	part->type = FLOAT;
	part->u.dval = part->u.ival;
}

static void add_stats(struct line *line, struct pattern *p)
{
	size_t i;

	line->count++;
	for (i = 0; i < line->pattern->num_parts; i++) {
		if (p->part[i].type == LITERAL)
			continue;
		if (p->part[i].type == FLOAT
		    && line->min[i].type == INTEGER) {
			convert_to_float(&line->min[i]);
			convert_to_float(&line->max[i]);
			convert_to_float(&line->total[i]);
		} else if (p->part[i].type == INTEGER
		    && line->min[i].type == FLOAT) {
			convert_to_float(&p->part[i]);
		}
		assert(p->part[i].type == line->min[i].type);
		assert(p->part[i].type == line->max[i].type);
		assert(p->part[i].type == line->total[i].type);
		if (p->part[i].type == INTEGER) {
			long long ival = p->part[i].u.ival;
			if (ival < line->min[i].u.ival)
				line->min[i].u.ival = ival;
			if (ival > line->max[i].u.ival)
				line->max[i].u.ival = ival;
			line->total[i].u.ival += ival;
		} else {
			double dval = p->part[i].u.dval;
			assert(p->part[i].type == FLOAT);
			if (dval < line->min[i].u.dval)
				line->min[i].u.dval = dval;
			if (dval > line->max[i].u.dval)
				line->max[i].u.dval = dval;
			line->total[i].u.dval += dval;
		}
	}
}

static void add_line(struct file *info, const char *str)
{
	struct line *line;
	struct pattern *p;

	p = get_pattern(str);

	line = linehash_get(&info->patterns, p);
	if (line) {
		add_stats(line, p);
	} else {
		size_t bytes = sizeof (*line->min) * p->num_parts;

		line = malloc(sizeof(*line) + bytes * 3);
		line->pattern = p;
		line->count = 1;
		line->min = (void *)line + sizeof(*line);
		line->max = line->min + line->pattern->num_parts;
		line->total = line->max + line->pattern->num_parts;
		memcpy(line->min, p->part, bytes);
		memcpy(line->max, p->part, bytes);
		memcpy(line->total, p->part, bytes);
		linehash_add(&info->patterns, line);
		list_add_tail(&info->lines, &line->list);
	}
}

static void print_analysis(const struct file *info, bool trim_outliers)
{
	struct line *l;

	/* FIXME: trim_outliers! */
	list_for_each(&info->lines, l, list) {
		size_t i;

		for (i = 0; i < l->pattern->num_parts; i++) {
			switch (l->min[i].type) {
			case LITERAL:
				printf("%.*s",
				       (int)l->pattern->part[i].len,
				       l->pattern->part[i].u.sval);
				break;
			case FLOAT:
				if (l->min[i].u.dval == l->max[i].u.dval)
					/* FIXME: keep literal for this */
					printf("%*lf", (int)l->min[i].len,
					       l->min[i].u.dval);
				else
					printf(" %lf-%lf(%lf)",
					       l->min[i].u.dval,
					       l->max[i].u.dval,
					       l->total[i].u.dval / l->count);
				break;
			case INTEGER:
				if (l->min[i].u.ival == l->max[i].u.ival)
					/* FIXME: keep literal for this */
					printf("%*lli", (int)l->min[i].len,
					       l->min[i].u.ival);
				else
					printf(" %lli-%lli(%lli)",
					       l->min[i].u.ival,
					       l->max[i].u.ival,
					       l->total[i].u.ival / l->count);
				break;
			default:
				abort();
			}

		}
		fputc('\n', stdout);
	}
}

int main(int argc, char *argv[])
{
	bool trim_outliers = false;

	opt_register_noarg("--trim-outliers", opt_set_bool, &trim_outliers,
			   "Remove max and min results from average");
	opt_register_noarg("-h|--help", opt_usage_and_exit,
			   "\nA program to print max-min(avg) stats in place"
			   "of numbers in a stream", "Print this message");
	opt_parse(&argc, argv, opt_log_stderr_exit);

	do {
		struct file info;
		struct rbuf in;
		char *str;

		list_head_init(&info.lines);
		linehash_init(&info.patterns);

		if (argv[1]) {
			if (!rbuf_open(&in, argv[1], NULL, 0))
				err(1, "Failed opening %s", argv[1]);
		} else
			rbuf_init(&in, STDIN_FILENO, NULL, 0);

		while ((str = rbuf_read_str(&in, '\n', realloc)) != NULL)
			add_line(&info, str);

		if (errno)
			err(1, "Reading %s", argv[1] ? argv[1] : "<stdin>");

		print_analysis(&info, trim_outliers);
	} while (argv[1] && (++argv)[1]);
	return 0;
}
