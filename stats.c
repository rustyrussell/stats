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

union val {
	long long ival;
	double dval;
};

struct pattern_part {
	enum pattern_type type;
	size_t off, len;
};

struct pattern {
	const char *text;
	size_t num_parts;
	struct pattern_part part[ /* num_parts */ ];
};

struct values {
	struct list_node list;
	union val vals[ /* num_parts */ ];
};

struct line {
	struct list_node list;
	struct pattern *pattern;

	struct list_head vals;
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
			h = hash(p->text + part->off, part->len, h);
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
			if (strncmp(p->text + part1->off,
				    p2->text + part2->off,
				    part1->len))
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

static inline size_t valsize(size_t num)
{
	return sizeof(struct values) + sizeof(union val) * num;
}

static void add_part(struct pattern **p, struct values **vals,
		     const struct pattern_part *part, const union val *v,
		     size_t *max_parts)
{
	if ((*p)->num_parts == *max_parts) {
		*max_parts *= 2;
		*p = realloc(*p, partsize(*max_parts));
		*vals = realloc(*vals, valsize(*max_parts));
	}
	(*vals)->vals[(*p)->num_parts] = *v;
	(*p)->part[(*p)->num_parts++] = *part;
}

/* We want "finished in100 seconds to match "finished in  5 seconds". */
struct pattern *get_pattern(const char *line, struct values **vals)
{
	enum pattern_type state = LITERAL;
	size_t len, i, max_parts = 3;
	struct pattern_part part;
	struct pattern *p;

	*vals = malloc(valsize(max_parts));
	p = malloc(partsize(max_parts));
	p->text = line;
	p->num_parts = 0;

	for (i = len = 0; state != TERM; i++, len++) {
		enum pattern_type old_state = state;
		bool starts_num;
		union val v;

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
		part.off = i - len;
		if (old_state == FLOAT) {
			char *end;
			v.dval = strtod(line + part.off, &end);
			if (end != line + i) {
				warnx("Could not parse float '%.*s'",
				      (int)len, line + i - len);
			} else {
				add_part(&p, vals, &part, &v, &max_parts);
			}
			len = 0;
		} else if (old_state == INTEGER) {
			char *end;
			v.ival = strtoll(line + part.off, &end, 10);
			if (end != line + i) {
				warnx("Could not parse integer '%.*s'",
				      (int)len, line + i - len);
			} else {
				add_part(&p, vals, &part, &v, &max_parts);
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
			add_part(&p, vals, &part, &v, &max_parts);
			len = 0;
		}
	}
	return p;
}

static void val_to_float(union val *val)
{
	val->dval = val->ival;
}

static void add_stats(struct line *line, struct pattern *p, struct values *vals)
{
	size_t i;

	for (i = 0; i < p->num_parts; i++) {
		if (p->part[i].type == LITERAL)
			continue;
		if (p->part[i].type == FLOAT
		    && line->pattern->part[i].type == INTEGER) {
			struct values *v;

			/* Convert all previous entries to float. */
			list_for_each(&line->vals, v, list)
				val_to_float(&v->vals[i]);
			line->pattern->part[i].type = FLOAT;
		} else if (p->part[i].type == INTEGER
			   && line->pattern->part[i].type == FLOAT) {
			val_to_float(&vals->vals[i]);
			p->part[i].type = FLOAT;
		}
		assert(p->part[i].type == line->pattern->part[i].type);
	}
	list_add_tail(&line->vals, &vals->list);
}

static void add_line(struct file *info, const char *str)
{
	struct line *line;
	struct pattern *p;
	struct values *vals;

	p = get_pattern(str, &vals);

	line = linehash_get(&info->patterns, p);
	if (line) {
		add_stats(line, p, vals);
	} else {
		/* We need to keep a copy of this! */
		p->text = strdup(p->text); 
		line = malloc(sizeof(*line));
		line->pattern = p;
		list_head_init(&line->vals);
		list_add(&line->vals, &vals->list);
		linehash_add(&info->patterns, line);
		list_add_tail(&info->lines, &line->list);
	}
}

static void print_literal_part(const struct pattern *p, size_t off)
{
	printf("%.*s", (int)p->part[off].len, p->text + p->part[off].off);
}

static bool spacestart(const struct pattern *p, size_t off)
{
	return cisspace(p->text[p->part[off].off]);
}

static inline bool greater_double(union val v1, union val v2)
{
	return v1.dval > v2.dval;
}

static inline union val add_double(union val v1, union val v2)
{
	union val v;
	v.dval = v1.dval + v2.dval;
	return v;
}

static inline union val sub_double(union val v1, union val v2)
{
	union val v;
	v.dval = v1.dval - v2.dval;
	return v;
}

static inline union val div_double(union val v, size_t num)
{
	v.dval /= num;
	return v;
}

static inline void print_double(union val val)
{
	printf("%lf", val.dval);
}

static inline bool greater_int(union val v1, union val v2)
{
	return v1.ival > v2.ival;
}

static inline union val add_int(union val v1, union val v2)
{
	union val v;
	v.ival = v1.ival + v2.ival;
	return v;
}

static inline union val sub_int(union val v1, union val v2)
{
	union val v;
	v.ival = v1.ival - v2.ival;
	return v;
}

static inline union val div_int(union val v, size_t num)
{
	v.ival /= num;
	return v;
}

static inline void print_int(union val val)
{
	printf("%lli", val.ival);
}

static void print_val(const struct list_head *vals, const struct pattern *p,
		      size_t off,
		      bool trim_outliers,
		      bool (*greater)(union val v1, union val v2),
		      union val (*add)(union val v1, union val v2),
		      union val (*sub)(union val v1, union val v2),
		      union val (*div)(union val v, size_t num),
		      void (*print)(union val v))
{
	size_t num = 0;
	union val min, max, tot;
	struct values *v;

	v = list_top(vals, struct values, list);
	list_for_each(vals, v, list) {
		if (!num) {
			min = max = tot = v->vals[off];
		} else {
			if (greater(min, v->vals[off]))
				min = v->vals[off];
			else if (greater(v->vals[off], max))
				max = v->vals[off];
			tot = add(tot, v->vals[off]);
		}
		num++;
	}

	if (memcmp(&min, &max, sizeof(min)) == 0)
		print_literal_part(p, off);
	else {
		if (spacestart(p, off))
			fputc(' ', stdout);
		print(min);
		fputc('-', stdout);
		print(max);
		fputc('(', stdout);
		if (num >= 3 && trim_outliers) {
			tot = sub(tot, max);
			tot = sub(tot, min);
			num -= 2;
		}
		print(div(tot, num));
		fputc(')', stdout);
	}
}

static void print_analysis(const struct file *info, bool trim_outliers)
{
	struct line *l;

	list_for_each(&info->lines, l, list) {
		size_t i;

		for (i = 0; i < l->pattern->num_parts; i++) {
			switch (l->pattern->part[i].type) {
			case LITERAL:
				print_literal_part(l->pattern, i);
				break;
			case FLOAT:
				print_val(&l->vals, l->pattern, i,
					  trim_outliers,
					  greater_double, add_double, sub_double,
					  div_double, print_double);
				break;
			case INTEGER:
				print_val(&l->vals, l->pattern, i,
					  trim_outliers,
					  greater_int, add_int, sub_int,
					  div_int, print_int);
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
