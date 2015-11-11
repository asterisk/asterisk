"""expand accountcode to 80

Revision ID: 54cde9847798
Revises: 210693f3123d
Create Date: 2015-11-05 09:54:06.815364

"""

# revision identifiers, used by Alembic.
revision = '54cde9847798'
down_revision = '210693f3123d'

from alembic import op
import sqlalchemy as sa


def upgrade():
    op.alter_column('cdr', 'accountcode', type_=sa.String(80))
    op.alter_column('cdr', 'peeraccount', type_=sa.String(80))
    pass


def downgrade():
    op.alter_column('cdr', 'accountcode', type_=sa.String(20))
    op.alter_column('cdr', 'peeraccount', type_=sa.String(20))
    pass
