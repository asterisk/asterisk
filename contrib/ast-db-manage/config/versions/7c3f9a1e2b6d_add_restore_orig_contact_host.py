"""Add restore_orig_contact_host to ps_endpoints

Revision ID: 7c3f9a1e2b6d
Revises: 2b45fd748a4f
Create Date: 2026-11-18 20:00:00.000000

"""

# revision identifiers, used by Alembic.
revision = '7c3f9a1e2b6d'
down_revision = '2b45fd748a4f'

from alembic import op
import sqlalchemy as sa
from sqlalchemy.dialects.postgresql import ENUM

YESNO_NAME = 'yesno_values'
YESNO_VALUES = ['yes', 'no']


def upgrade():
    yesno_values = ENUM(*YESNO_VALUES, name=YESNO_NAME, create_type=False)

    op.add_column('ps_endpoints', sa.Column('restore_orig_contact_host', yesno_values))


def downgrade():
    if op.get_context().bind.dialect.name == 'mssql':
        op.drop_constraint('ck_ps_endpoints_restore_orig_contact_host_yesno_values', 'ps_endpoints')
    op.drop_column('ps_endpoints', 'restore_orig_contact_host')
