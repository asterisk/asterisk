"""update_identify_by

Revision ID: 3772f8f828da
Revises: c7a44a5a0851
Create Date: 2016-08-11 10:47:29.211063

"""

# revision identifiers, used by Alembic.
revision = '3772f8f828da'
down_revision = 'c7a44a5a0851'

from alembic import op
import sqlalchemy as sa


def enum_update(table_name, column_name, enum_name, enum_values):
    if op.get_context().bind.dialect.name != 'postgresql':
        if op.get_context().bind.dialect.name == 'mssql':
            op.drop_constraint('ck_ps_endpoints_identify_by_pjsip_identify_by_values', 'ps_endpoints')
        op.alter_column(table_name, column_name,
                        type_=sa.Enum(*enum_values, name=enum_name))
        return

    # Postgres requires a few more steps
    tmp = enum_name + '_tmp'

    op.execute('ALTER TYPE ' + enum_name + ' RENAME TO ' + tmp)

    updated = sa.Enum(*enum_values, name=enum_name)
    updated.create(op.get_bind(), checkfirst=False)

    op.execute('ALTER TABLE ' + table_name + ' ALTER COLUMN ' + column_name +
               ' TYPE ' + enum_name + ' USING identify_by::text::' + enum_name)

    op.execute('DROP TYPE ' + tmp)


def upgrade():
    enum_update('ps_endpoints', 'identify_by', 'pjsip_identify_by_values',
                ['username', 'auth_username'])


def downgrade():
    enum_update('ps_endpoints', 'identify_by', 'pjsip_identify_by_values',
                ['username'])
